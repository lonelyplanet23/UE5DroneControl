#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config.hpp"
#include "models.hpp"
#include "udp_stub.hpp"
#include "ws_manager.hpp"

struct ApiError : public std::runtime_error {
    int status_code = 500;

    ApiError(int status, const std::string& message)
        : std::runtime_error(message), status_code(status) {}
};

class Logger {
public:
    Logger(const std::string& level, const std::string& file_path)
        : level_(normalize_level(level)) {
        if (!file_path.empty()) {
            std::filesystem::path path(file_path);
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            file_.open(path, std::ios::app);
        }
    }

    void debug(const std::string& message) { log("debug", message); }
    void info(const std::string& message) { log("info", message); }
    void warn(const std::string& message) { log("warn", message); }
    void error(const std::string& message) { log("error", message); }

private:
    int normalize_level(const std::string& level) const {
        if (level == "debug") {
            return 0;
        }
        if (level == "info") {
            return 1;
        }
        if (level == "warn") {
            return 2;
        }
        return 3;
    }

    int severity(const std::string& level) const {
        return normalize_level(level);
    }

    std::string now_string() const {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm tm_value{};
#ifdef _WIN32
        localtime_s(&tm_value, &time);
#else
        localtime_r(&time, &tm_value);
#endif
        std::ostringstream stream;
        stream << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S");
        return stream.str();
    }

    void log(const std::string& level, const std::string& message) {
        if (severity(level) < level_) {
            return;
        }

        const std::string line = "[" + now_string() + "] [" + level + "] " + message;
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << line << std::endl;
        if (file_.is_open()) {
            file_ << line << '\n';
            file_.flush();
        }
    }

    std::mutex mutex_;
    std::ofstream file_;
    int level_ = 1;
};

inline std::string json_stringify(const boost::json::value& value) {
    return boost::json::serialize(value);
}

inline double json_number(const boost::json::value& value) {
    if (value.is_double()) {
        return value.as_double();
    }
    if (value.is_int64()) {
        return static_cast<double>(value.as_int64());
    }
    if (value.is_uint64()) {
        return static_cast<double>(value.as_uint64());
    }
    throw ApiError(400, "expected number");
}

inline int json_int(const boost::json::value& value) {
    return static_cast<int>(std::llround(json_number(value)));
}

inline std::string json_string(const boost::json::value& value) {
    if (!value.is_string()) {
        throw ApiError(400, "expected string");
    }
    return std::string(value.as_string().c_str());
}

inline bool json_bool(const boost::json::value& value) {
    if (value.is_bool()) {
        return value.as_bool();
    }
    if (value.is_int64()) {
        return value.as_int64() != 0;
    }
    if (value.is_uint64()) {
        return value.as_uint64() != 0;
    }
    if (value.is_string()) {
        const std::string text = std::string(value.as_string().c_str());
        return text == "true" || text == "1" || text == "True";
    }
    throw ApiError(400, "expected bool");
}

inline const boost::json::object& require_object(const boost::json::value& value, const std::string& name) {
    if (!value.is_object()) {
        throw ApiError(400, name + " must be an object");
    }
    return value.as_object();
}

inline const boost::json::array& require_array(const boost::json::value& value, const std::string& name) {
    if (!value.is_array()) {
        throw ApiError(400, name + " must be an array");
    }
    return value.as_array();
}

class BackendService {
public:
    BackendService(Config config, WsManager* ws_manager, Logger* logger)
        : config_(std::move(config)), ws_manager_(ws_manager), logger_(logger) {
        load_storage_locked();
    }

    ~BackendService() {
        stop_background_threads();
    }

    void start_background_threads() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return;
        }

        heartbeat_thread_ = std::thread([this]() { heartbeat_loop(); });
        timeout_thread_ = std::thread([this]() { timeout_loop(); });
    }

    void stop_background_threads() {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) {
            return;
        }

        if (heartbeat_thread_.joinable()) {
            heartbeat_thread_.join();
        }
        if (timeout_thread_.joinable()) {
            timeout_thread_.join();
        }
    }

    bool running() const {
        return running_.load();
    }

    const Config& config() const {
        return config_;
    }

    boost::json::object list_drones() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DroneRecord> drones;
        drones.reserve(drones_.size());
        for (const auto& item : drones_) {
            drones.push_back(item.second.record);
        }

        std::sort(drones.begin(), drones.end(), [](const DroneRecord& left, const DroneRecord& right) {
            if (left.slot == right.slot) {
                return left.id < right.id;
            }
            return left.slot < right.slot;
        });

        boost::json::array array;
        for (const auto& drone : drones) {
            array.push_back(drone.to_public_json());
        }

        return {{"drones", array}};
    }

    boost::json::object register_drone(const boost::json::object& request) {
        std::lock_guard<std::mutex> lock(mutex_);

        const std::string name = required_string(request, "name");
        const std::string model = required_string(request, "model");
        const int slot = required_int(request, "slot");
        const std::string ip = required_string(request, "ip");
        const int port = required_int(request, "port");
        const std::string video_url = optional_string(request, "video_url", "");

        if (slot < 1 || slot > config_.max_count) {
            throw ApiError(400, "slot must be between 1 and " + std::to_string(config_.max_count));
        }
        if (config_.send_port_for_slot(slot) == 0 || config_.recv_port_for_slot(slot) == 0) {
            throw ApiError(400, "slot is not mapped in config.yaml");
        }
        if (static_cast<int>(drones_.size()) >= config_.max_count) {
            throw ApiError(507, "maximum drone count reached");
        }

        ensure_unique_name_locked(name, "");
        ensure_unique_slot_locked(slot, "");

        DroneRecord record;
        record.id = "d" + std::to_string(next_drone_id_++);
        record.name = name;
        record.model = model;
        record.slot = slot;
        record.ip = ip;
        record.port = port;
        record.video_url = video_url;
        record.status = DroneStatus::offline;
        record.battery = -1;

        DroneRuntime runtime;
        runtime.record = record;
        drones_[record.id] = runtime;
        save_storage_locked();

        if (logger_ != nullptr) {
            logger_->info("registered drone " + record.id + " on slot " + std::to_string(slot));
        }

        return {
            {"id", record.id},
            {"slot", record.slot},
        };
    }

    boost::json::object update_drone(const std::string& id, const boost::json::object& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& drone = require_drone_locked(id);

        if (request.contains("name")) {
            const std::string name = json_string(request.at("name"));
            ensure_unique_name_locked(name, id);
            drone.record.name = name;
        }
        if (request.contains("model")) {
            drone.record.model = json_string(request.at("model"));
        }
        if (request.contains("slot")) {
            const int slot = json_int(request.at("slot"));
            if (slot < 1 || slot > config_.max_count) {
                throw ApiError(400, "slot must be between 1 and " + std::to_string(config_.max_count));
            }
            ensure_unique_slot_locked(slot, id);
            drone.record.slot = slot;
        }
        if (request.contains("ip")) {
            drone.record.ip = json_string(request.at("ip"));
        }
        if (request.contains("port")) {
            drone.record.port = json_int(request.at("port"));
        }
        if (request.contains("video_url")) {
            drone.record.video_url = json_string(request.at("video_url"));
        }

        save_storage_locked();

        return {
            {"id", id},
            {"updated", true},
        };
    }

    boost::json::object delete_drone(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = drones_.find(id);
        if (it == drones_.end()) {
            throw ApiError(404, "drone not found");
        }

        if (!it->second.active_array_id.empty()) {
            stop_array_locked(it->second.active_array_id);
        }

        drones_.erase(it);
        save_storage_locked();

        return {
            {"id", id},
            {"deleted", true},
        };
    }

    boost::json::object get_anchor(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& drone = require_drone_locked(id);
        if (!drone.record.anchor.available) {
            throw ApiError(425, "anchor not available");
        }
        return drone.record.anchor.to_json(id);
    }

    boost::json::object create_array_from_http(const boost::json::object& request) {
        auto create_request = parse_http_array_request(request);
        return create_array(std::move(create_request));
    }

    boost::json::object stop_array(const std::string& array_id) {
        std::vector<std::string> drones_to_hover;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = arrays_.find(array_id);
            if (it == arrays_.end()) {
                throw ApiError(404, "array not found");
            }
            if (it->second.status != ArrayStatus::stopped) {
                drones_to_hover = stop_array_locked(array_id);
            }
        }

        dispatch_hover_packets(drones_to_hover);

        return {
            {"array_id", array_id},
            {"status", "stopped"},
        };
    }

    boost::json::object debug_drone_state(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& drone = require_drone_locked(id);

        boost::json::array queue_json;
        for (const auto& command : drone.queue) {
            queue_json.push_back(command.to_json());
        }

        boost::json::object response{
            {"drone", drone.record.to_public_json()},
            {"paused", drone.paused},
            {"queue", queue_json},
            {"queue_size", static_cast<std::int64_t>(drone.queue.size())},
            {"heartbeat_count", static_cast<std::int64_t>(drone.heartbeat_count)},
            {"last_telemetry_age_ms", age_ms(drone.last_telemetry_at)},
            {"last_heartbeat_age_ms", age_ms(drone.last_heartbeat_at)},
            {"anchor_available", drone.record.anchor.available},
            {"active_array_id", drone.active_array_id},
            {"slot_ports", boost::json::object{
                {"send_port", config_.send_port_for_slot(drone.record.slot)},
                {"recv_port", config_.recv_port_for_slot(drone.record.slot)},
            }},
        };

        if (drone.last_telemetry.has_value()) {
            response["last_telemetry"] = telemetry_debug_json(*drone.last_telemetry);
        }

        if (!drone.active_array_id.empty()) {
            auto array_it = arrays_.find(drone.active_array_id);
            if (array_it != arrays_.end()) {
                response["array"] = array_it->second.to_json();
            }
        }

        return response;
    }

    boost::json::object debug_drone_queue(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& drone = require_drone_locked(id);
        boost::json::array queue_json;
        for (const auto& command : drone.queue) {
            queue_json.push_back(command.to_json());
        }

        return {
            {"drone_id", id},
            {"queue", queue_json},
        };
    }

    boost::json::object debug_heartbeat(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& drone = require_drone_locked(id);

        double effective_hz = 0.0;
        if (drone.first_heartbeat_at.has_value() && drone.last_heartbeat_at.has_value() && drone.heartbeat_count > 1) {
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                *drone.last_heartbeat_at - *drone.first_heartbeat_at
            ).count();
            if (elapsed_ms > 0) {
                effective_hz = static_cast<double>(drone.heartbeat_count - 1) / (static_cast<double>(elapsed_ms) / 1000.0);
            }
        }

        return {
            {"drone_id", id},
            {"count", static_cast<std::int64_t>(drone.heartbeat_count)},
            {"configured_hz", config_.heartbeat_hz},
            {"effective_hz", effective_hz},
            {"last_send_age_ms", age_ms(drone.last_heartbeat_at)},
        };
    }

    boost::json::object inject_telemetry(const std::string& id, const boost::json::object& request) {
        TelemetryFrame frame;
        if (request.contains("position")) {
            const auto& position = require_array(request.at("position"), "position");
            if (position.size() != 3) {
                throw ApiError(400, "position must have 3 numbers");
            }
            frame.position = {json_number(position[0]), json_number(position[1]), json_number(position[2])};
        }
        if (request.contains("q")) {
            const auto& q = require_array(request.at("q"), "q");
            if (q.size() != 4) {
                throw ApiError(400, "q must have 4 numbers");
            }
            frame.q = {json_number(q[0]), json_number(q[1]), json_number(q[2]), json_number(q[3])};
        }
        if (request.contains("velocity")) {
            const auto& velocity = require_array(request.at("velocity"), "velocity");
            if (velocity.size() != 3) {
                throw ApiError(400, "velocity must have 3 numbers");
            }
            frame.velocity = {json_number(velocity[0]), json_number(velocity[1]), json_number(velocity[2])};
        }
        if (request.contains("angular_velocity")) {
            const auto& angular_velocity = require_array(request.at("angular_velocity"), "angular_velocity");
            if (angular_velocity.size() != 3) {
                throw ApiError(400, "angular_velocity must have 3 numbers");
            }
            frame.angular_velocity = {json_number(angular_velocity[0]), json_number(angular_velocity[1]), json_number(angular_velocity[2])};
        }
        if (request.contains("battery")) {
            frame.battery = json_int(request.at("battery"));
        }
        if (request.contains("gps_lat") || request.contains("gps_lon") || request.contains("gps_alt")) {
            GeoAnchor anchor;
            anchor.available = true;
            anchor.gps_lat = optional_number(request, "gps_lat", 0.0);
            anchor.gps_lon = optional_number(request, "gps_lon", 0.0);
            anchor.gps_alt = optional_number(request, "gps_alt", 0.0);
            frame.anchor = anchor;
        }
        if (request.contains("timestamp")) {
            frame.timestamp_us = static_cast<std::uint64_t>(std::max(0, json_int(request.at("timestamp"))));
        }

        ingest_telemetry(id, frame);

        return {
            {"drone_id", id},
            {"status", "online"},
            {"ws_clients", static_cast<std::int64_t>(ws_manager_ != nullptr ? ws_manager_->count() : 0)},
        };
    }

    boost::json::object debug_move(const std::string& id, const boost::json::object& request) {
        const double x = required_number(request, "x");
        const double y = required_number(request, "y");
        const double z = required_number(request, "z");
        queue_manual_move(id, ue_to_ned(x, y, z), "debug_move");

        return {
            {"drone_id", id},
            {"queued", true},
            {"ned", boost::json::object{
                {"x", x * 0.01},
                {"y", y * 0.01},
                {"z", z * -0.01},
            }},
        };
    }

    boost::json::object debug_pause(const std::string& id, bool paused) {
        std::vector<std::string> hover_targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& drone = require_drone_locked(id);
            drone.paused = paused;
            if (paused) {
                hover_targets.push_back(id);
            }
        }

        if (paused) {
            dispatch_hover_packets(hover_targets);
        } else {
            dispatch_current_packet(id, false);
        }

        return {
            {"drone_id", id},
            {"paused", paused},
        };
    }

    boost::json::object debug_single_array(const std::string& id, const boost::json::object& request) {
        CreateArrayRequest array_request;
        array_request.array_id = next_array_id("dbg");
        array_request.mode = optional_string(request, "mode", "recon");
        array_request.paths.push_back(parse_debug_single_path(id, request, 1, array_request.mode));
        return create_array(std::move(array_request));
    }

    boost::json::object debug_target(const std::string& id, const boost::json::object& request) {
        const double x = required_number(request, "x");
        const double y = required_number(request, "y");
        const double z = required_number(request, "z");

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& drone = require_drone_locked(id);
            if (drone.active_array_id.empty()) {
                throw ApiError(409, "drone is not executing a patrol array");
            }

            auto array_it = arrays_.find(drone.active_array_id);
            if (array_it == arrays_.end()) {
                throw ApiError(409, "array state not found");
            }

            auto* path = find_array_path(array_it->second, id);
            if (path == nullptr || path->mode != "patrol") {
                throw ApiError(409, "target injection is only available in patrol mode");
            }

            path->target_override_active = true;
            path->target_override = CommandTarget{
                ue_to_ned(x, y, z),
                1,
                "patrol_target",
                path->path_id,
                0.0,
                0.0,
            };

            drone.queue.clear();
            drone.queue.push_back(path->target_override);
        }

        dispatch_current_packet(id, false);

        return {
            {"drone_id", id},
            {"target_set", true},
        };
    }

    boost::json::object debug_batch_array(const boost::json::array& request) {
        if (request.empty()) {
            throw ApiError(400, "batch array request cannot be empty");
        }

        CreateArrayRequest array_request;
        array_request.array_id = next_array_id("dbg-batch");

        std::string common_mode;
        int next_path_id = 1;
        for (const auto& item : request) {
            const auto& object = require_object(item, "batch item");
            const std::string drone_id = required_string(object, "drone_id");
            const std::string mode = optional_string(object, "mode", "recon");
            array_request.paths.push_back(parse_debug_single_path(drone_id, object, next_path_id++, mode));
            if (common_mode.empty()) {
                common_mode = mode;
            } else if (common_mode != mode) {
                common_mode = "mixed";
            }
        }

        array_request.mode = common_mode.empty() ? "recon" : common_mode;
        return create_array(std::move(array_request));
    }

    boost::json::object debug_array_state(const std::string& array_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = arrays_.find(array_id);
        if (it == arrays_.end()) {
            throw ApiError(404, "array not found");
        }
        return it->second.to_json();
    }

    void handle_ws_command(const boost::json::object& message) {
        const std::string type = required_string(message, "type");

        if (type == "move") {
            const std::string drone_id = required_string(message, "drone_id");
            const double x = required_number(message, "x");
            const double y = required_number(message, "y");
            const double z = required_number(message, "z");
            queue_manual_move(drone_id, ue_to_ned(x, y, z), "ws_move");
            return;
        }

        if (type == "pause" || type == "resume") {
            if (!message.contains("drone_ids")) {
                throw ApiError(400, "drone_ids is required");
            }
            const auto& drone_ids = require_array(message.at("drone_ids"), "drone_ids");
            if (drone_ids.empty()) {
                throw ApiError(400, "drone_ids cannot be empty");
            }

            const bool paused = (type == "pause");
            std::vector<std::string> ids;
            ids.reserve(drone_ids.size());
            for (const auto& value : drone_ids) {
                ids.push_back(json_string(value));
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& id : ids) {
                    auto& drone = require_drone_locked(id);
                    drone.paused = paused;
                }
            }

            if (paused) {
                dispatch_hover_packets(ids);
            } else {
                for (const auto& id : ids) {
                    dispatch_current_packet(id, false);
                }
            }
            return;
        }

        throw ApiError(400, "unsupported websocket message type");
    }

    void handle_udp_telemetry(int slot, const std::string& payload) {
        std::string drone_id;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& item : drones_) {
                if (item.second.record.slot == slot) {
                    drone_id = item.first;
                    break;
                }
            }
        }

        if (drone_id.empty()) {
            return;
        }

        TelemetryFrame frame;
        std::string error;
        if (!parse_yaml_telemetry(payload, frame, &error)) {
            if (logger_ != nullptr) {
                logger_->warn("failed to parse telemetry for slot " + std::to_string(slot) + ": " + error);
            }
            return;
        }

        ingest_telemetry(drone_id, frame);
    }

private:
    struct CreateArrayRequest {
        std::string array_id;
        std::string mode;
        std::vector<ArrayPathState> paths;
    };

    struct PendingPacket {
        std::string drone_id;
        std::string ip;
        int send_port = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::uint32_t mode = 0;
        bool heartbeat_tick = false;
    };

    static std::int64_t age_ms(const std::optional<std::chrono::steady_clock::time_point>& point) {
        if (!point.has_value()) {
            return -1;
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - *point
        ).count();
    }

    boost::json::object telemetry_debug_json(const TelemetryFrame& frame) const {
        return {
            {"timestamp_us", static_cast<std::int64_t>(frame.timestamp_us)},
            {"position", boost::json::array{frame.position.x, frame.position.y, frame.position.z}},
            {"q", boost::json::array{frame.q.w, frame.q.x, frame.q.y, frame.q.z}},
            {"velocity", boost::json::array{frame.velocity.x, frame.velocity.y, frame.velocity.z}},
            {"angular_velocity", boost::json::array{frame.angular_velocity.x, frame.angular_velocity.y, frame.angular_velocity.z}},
            {"battery", frame.battery},
        };
    }

    std::string required_string(const boost::json::object& object, const std::string& key) const {
        if (!object.contains(key)) {
            throw ApiError(400, key + " is required");
        }
        return json_string(object.at(key));
    }

    double required_number(const boost::json::object& object, const std::string& key) const {
        if (!object.contains(key)) {
            throw ApiError(400, key + " is required");
        }
        return json_number(object.at(key));
    }

    int required_int(const boost::json::object& object, const std::string& key) const {
        if (!object.contains(key)) {
            throw ApiError(400, key + " is required");
        }
        return json_int(object.at(key));
    }

    std::string optional_string(const boost::json::object& object, const std::string& key, const std::string& fallback) const {
        if (!object.contains(key)) {
            return fallback;
        }
        return json_string(object.at(key));
    }

    double optional_number(const boost::json::object& object, const std::string& key, double fallback) const {
        if (!object.contains(key)) {
            return fallback;
        }
        return json_number(object.at(key));
    }

    void ensure_unique_name_locked(const std::string& name, const std::string& exclude_id) const {
        for (const auto& item : drones_) {
            if (item.first != exclude_id && item.second.record.name == name) {
                throw ApiError(409, "name already exists");
            }
        }
    }

    void ensure_unique_slot_locked(int slot, const std::string& exclude_id) const {
        for (const auto& item : drones_) {
            if (item.first != exclude_id && item.second.record.slot == slot) {
                throw ApiError(409, "slot already exists");
            }
        }
    }

    DroneRuntime& require_drone_locked(const std::string& id) {
        auto it = drones_.find(id);
        if (it == drones_.end()) {
            throw ApiError(404, "drone not found");
        }
        return it->second;
    }

    const DroneRuntime& require_drone_locked(const std::string& id) const {
        auto it = drones_.find(id);
        if (it == drones_.end()) {
            throw ApiError(404, "drone not found");
        }
        return it->second;
    }

    ArrayPathState* find_array_path(ArrayTask& task, const std::string& drone_id) const {
        for (auto& path : task.paths) {
            if (path.drone_id == drone_id) {
                return &path;
            }
        }
        return nullptr;
    }

    const ArrayPathState* find_array_path(const ArrayTask& task, const std::string& drone_id) const {
        for (const auto& path : task.paths) {
            if (path.drone_id == drone_id) {
                return &path;
            }
        }
        return nullptr;
    }

    void load_storage_locked() {
        drones_.clear();
        next_drone_id_ = 1;

        std::ifstream file(config_.storage_path);
        if (!file.is_open()) {
            return;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (content.empty()) {
            return;
        }

        try {
            const auto root = boost::json::parse(content);
            boost::json::array drones_json;

            if (root.is_object()) {
                const auto& object = root.as_object();
                if (object.contains("next_id")) {
                    next_drone_id_ = std::max(1, json_int(object.at("next_id")));
                }
                if (object.contains("drones")) {
                    drones_json = require_array(object.at("drones"), "drones");
                }
            } else if (root.is_array()) {
                drones_json = root.as_array();
            }

            for (const auto& item : drones_json) {
                const auto& object = require_object(item, "drone");

                DroneRecord record;
                record.id = optional_string(object, "id", "");
                record.name = optional_string(object, "name", "");
                record.model = optional_string(object, "model", "");
                record.slot = object.contains("slot") ? json_int(object.at("slot")) : (object.contains("slot_number") ? json_int(object.at("slot_number")) : 0);
                record.ip = optional_string(object, "ip", "");
                record.port = object.contains("port") ? json_int(object.at("port")) : 0;
                record.video_url = optional_string(object, "video_url", "");
                record.status = DroneStatus::offline;
                record.battery = -1;

                if (record.id.empty()) {
                    continue;
                }

                if (record.id.size() > 1 && record.id.front() == 'd') {
                    try {
                        next_drone_id_ = std::max(next_drone_id_, std::stoi(record.id.substr(1)) + 1);
                    } catch (...) {
                    }
                }

                DroneRuntime runtime;
                runtime.record = record;
                drones_[record.id] = runtime;
            }
        } catch (...) {
            if (logger_ != nullptr) {
                logger_->warn("failed to parse storage file, starting from empty registry");
            }
            drones_.clear();
            next_drone_id_ = 1;
        }
    }

    void save_storage_locked() const {
        std::filesystem::path path(config_.storage_path);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }

        boost::json::array array;
        for (const auto& item : drones_) {
            array.push_back(item.second.record.to_storage_json());
        }

        std::ofstream file(path);
        file << json_stringify(boost::json::object{
            {"next_id", next_drone_id_},
            {"drones", array},
        });
    }

    std::string next_array_id(const std::string& prefix) {
        std::lock_guard<std::mutex> lock(mutex_);
        return prefix + "-" + std::to_string(next_array_id_++);
    }

    ArrayPathState parse_debug_single_path(
        const std::string& drone_id,
        const boost::json::object& request,
        int path_id,
        const std::string& mode
    ) const {
        if (!request.contains("waypoints")) {
            throw ApiError(400, "waypoints is required");
        }

        const auto& waypoints = require_array(request.at("waypoints"), "waypoints");
        if (waypoints.empty()) {
            throw ApiError(400, "waypoints cannot be empty");
        }

        ArrayPathState path;
        path.path_id = path_id;
        path.drone_id = drone_id;
        path.mode = mode;
        path.closed_loop = request.contains("loop") ? json_bool(request.at("loop")) : false;

        for (const auto& item : waypoints) {
            const auto& waypoint = require_object(item, "waypoint");
            path.plan.push_back(CommandTarget{
                ue_to_ned(required_number(waypoint, "x"), required_number(waypoint, "y"), required_number(waypoint, "z")),
                1,
                "array",
                path_id,
                0.0,
                0.0,
            });
        }

        if (mode == "attack") {
            path.closed_loop = false;
        }

        return path;
    }

    CreateArrayRequest parse_http_array_request(const boost::json::object& request) const {
        if (!request.contains("paths")) {
            throw ApiError(400, "paths is required");
        }

        CreateArrayRequest array_request;
        array_request.array_id = optional_string(request, "array_id", "");
        array_request.mode = optional_string(request, "mode", "recon");

        const auto& paths = require_array(request.at("paths"), "paths");
        if (paths.empty()) {
            throw ApiError(400, "paths cannot be empty");
        }

        for (const auto& item : paths) {
            const auto& path_object = require_object(item, "path");

            ArrayPathState path;
            path.path_id = required_int(path_object, "pathId");
            path.drone_id = required_string(path_object, "drone_id");
            path.mode = array_request.mode;
            path.closed_loop = path_object.contains("bClosedLoop") ? json_bool(path_object.at("bClosedLoop")) : false;
            if (path.mode == "attack") {
                path.closed_loop = false;
            }

            if (!path_object.contains("waypoints")) {
                throw ApiError(400, "path.waypoints is required");
            }
            const auto& waypoints = require_array(path_object.at("waypoints"), "path.waypoints");
            if (waypoints.empty()) {
                throw ApiError(400, "path.waypoints cannot be empty");
            }

            for (const auto& waypoint_value : waypoints) {
                const auto& waypoint = require_object(waypoint_value, "waypoint");
                if (!waypoint.contains("location")) {
                    throw ApiError(400, "waypoint.location is required");
                }
                const auto& location = require_object(waypoint.at("location"), "location");

                path.plan.push_back(CommandTarget{
                    ue_to_ned(
                        required_number(location, "x"),
                        required_number(location, "y"),
                        required_number(location, "z")
                    ),
                    1,
                    "array",
                    path.path_id,
                    optional_number(waypoint, "segmentSpeed", 0.0),
                    optional_number(waypoint, "waitTime", 0.0),
                });
            }

            array_request.paths.push_back(path);
        }

        return array_request;
    }

    boost::json::object create_array(CreateArrayRequest request) {
        if (request.paths.empty()) {
            throw ApiError(400, "array must contain at least one path");
        }

        std::vector<std::string> drones_to_dispatch;
        std::vector<boost::json::object> outbound;
        std::string array_id;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (request.array_id.empty()) {
                request.array_id = "a" + std::to_string(next_array_id_++);
            }
            array_id = request.array_id;

            if (arrays_.count(array_id) != 0) {
                throw ApiError(409, "array_id already exists");
            }

            std::unordered_map<std::string, bool> seen_drones;
            for (const auto& path : request.paths) {
                if (path.plan.empty()) {
                    throw ApiError(400, "path waypoints cannot be empty");
                }
                require_drone_locked(path.drone_id);
                if (seen_drones[path.drone_id]) {
                    throw ApiError(409, "duplicate drone_id in array");
                }
                seen_drones[path.drone_id] = true;
            }

            for (const auto& path : request.paths) {
                auto& drone = require_drone_locked(path.drone_id);
                if (!drone.active_array_id.empty()) {
                    stop_array_locked(drone.active_array_id);
                }
                drone.queue.clear();
                drone.paused = false;
                drone.active_array_id = array_id;
                drone.record.status = drone.record.status == DroneStatus::offline ? DroneStatus::connecting : drone.record.status;
                for (const auto& command : path.plan) {
                    drone.queue.push_back(command);
                }
            }

            ArrayTask task;
            task.array_id = array_id;
            task.mode = request.mode;
            task.status = ArrayStatus::assembling;
            task.ready_count = 0;
            task.total_count = request.paths.size();
            task.created_at = std::chrono::steady_clock::now();
            task.paths = request.paths;

            arrays_[array_id] = task;

            outbound.push_back(boost::json::object{
                {"type", "assembling"},
                {"array_id", array_id},
                {"ready_count", 0},
                {"total_count", static_cast<std::int64_t>(task.total_count)},
            });

            for (const auto& path : request.paths) {
                drones_to_dispatch.push_back(path.drone_id);
            }
        }

        broadcast_messages(outbound);
        for (const auto& drone_id : drones_to_dispatch) {
            dispatch_current_packet(drone_id, false);
        }

        return {
            {"array_id", array_id},
            {"status", "assembling"},
        };
    }

    void queue_manual_move(const std::string& id, const NedPoint& target, const std::string& source) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& drone = require_drone_locked(id);
            if (!drone.active_array_id.empty()) {
                stop_array_locked(drone.active_array_id);
                drone.active_array_id.clear();
            }
            drone.queue.clear();
            drone.paused = false;
            drone.queue.push_back(CommandTarget{target, 1, source, 0, 0.0, 0.0});
            if (drone.record.status == DroneStatus::offline) {
                drone.record.status = DroneStatus::connecting;
            }
        }

        dispatch_current_packet(id, false);
    }

    void ingest_telemetry(const std::string& id, const TelemetryFrame& frame) {
        std::vector<boost::json::object> outbound;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& drone = require_drone_locked(id);

            const bool first_packet = !drone.last_telemetry_at.has_value();
            const bool was_lost = drone.record.status == DroneStatus::lost;

            drone.last_telemetry = frame;
            drone.last_telemetry_at = std::chrono::steady_clock::now();
            drone.record.status = DroneStatus::online;

            if (frame.battery >= 0) {
                drone.record.battery = frame.battery;
                if (frame.battery >= config_.low_battery_threshold) {
                    drone.low_battery_alerted = false;
                } else if (!drone.low_battery_alerted) {
                    outbound.push_back(boost::json::object{
                        {"type", "alert"},
                        {"drone_id", id},
                        {"alert", "low_battery"},
                        {"value", frame.battery},
                    });
                    drone.low_battery_alerted = true;
                }
            }

            if (frame.anchor.has_value() && frame.anchor->available) {
                drone.record.anchor = *frame.anchor;
            }

            outbound.push_back(make_telemetry_message(id, frame));

            if (first_packet && drone.record.anchor.available) {
                outbound.push_back(make_event_message(id, "power_on", drone.record.anchor));
            } else if (was_lost && drone.record.anchor.available) {
                outbound.push_back(make_event_message(id, "reconnect", drone.record.anchor));
                drone.lost_alerted = false;
            }

            on_position_update_locked(id, frame.position, outbound);
        }

        broadcast_messages(outbound);
    }

    boost::json::object make_telemetry_message(const std::string& id, const TelemetryFrame& frame) const {
        const UePoint ue = ned_to_ue(frame.position);
        const auto [yaw, pitch, roll] = quaternion_to_ue_euler_deg(frame.q);
        return {
            {"type", "telemetry"},
            {"drone_id", id},
            {"x", ue.x},
            {"y", ue.y},
            {"z", ue.z},
            {"yaw", yaw},
            {"pitch", pitch},
            {"roll", roll},
            {"speed", speed_mps(frame.velocity)},
            {"battery", frame.battery},
        };
    }

    boost::json::object make_event_message(const std::string& id, const std::string& event, const GeoAnchor& anchor) const {
        boost::json::object message{
            {"type", "event"},
            {"drone_id", id},
            {"event", event},
        };

        if ((event == "power_on" || event == "reconnect") && anchor.available) {
            message["gps_lat"] = anchor.gps_lat;
            message["gps_lon"] = anchor.gps_lon;
            message["gps_alt"] = anchor.gps_alt;
        }

        return message;
    }

    void on_position_update_locked(
        const std::string& drone_id,
        const NedPoint& current_position,
        std::vector<boost::json::object>& outbound
    ) {
        auto& drone = require_drone_locked(drone_id);
        if (drone.queue.empty()) {
            return;
        }

        if (distance_m(current_position, drone.queue.front().ned) > config_.arrival_threshold_m) {
            return;
        }

        if (!drone.active_array_id.empty()) {
            auto array_it = arrays_.find(drone.active_array_id);
            if (array_it == arrays_.end()) {
                drone.active_array_id.clear();
                drone.queue.pop_front();
                return;
            }

            auto* path = find_array_path(array_it->second, drone_id);
            if (path == nullptr) {
                drone.active_array_id.clear();
                drone.queue.pop_front();
                return;
            }

            if (array_it->second.status == ArrayStatus::assembling) {
                if (!path->ready) {
                    path->ready = true;
                    path->current_index = 1;
                    if (!drone.queue.empty()) {
                        drone.queue.pop_front();
                    }

                    array_it->second.ready_count += 1;
                    outbound.push_back(boost::json::object{
                        {"type", "assembling"},
                        {"array_id", array_it->second.array_id},
                        {"ready_count", static_cast<std::int64_t>(array_it->second.ready_count)},
                        {"total_count", static_cast<std::int64_t>(array_it->second.total_count)},
                    });

                    if (array_it->second.ready_count >= array_it->second.total_count) {
                        array_it->second.status = ArrayStatus::executing;
                        outbound.push_back(boost::json::object{
                            {"type", "assembly_complete"},
                            {"array_id", array_it->second.array_id},
                        });

                        for (auto& participant : array_it->second.paths) {
                            auto& participant_drone = require_drone_locked(participant.drone_id);
                            if (participant.plan.size() <= 1) {
                                participant.completed = true;
                                participant_drone.queue.clear();
                                participant_drone.active_array_id.clear();
                            } else {
                                participant_drone.queue.clear();
                                for (std::size_t i = 1; i < participant.plan.size(); ++i) {
                                    participant_drone.queue.push_back(participant.plan[i]);
                                }
                            }
                        }

                        finish_array_if_done_locked(array_it->second);
                    }
                }
                return;
            }

            if (array_it->second.status == ArrayStatus::executing) {
                if (path->target_override_active) {
                    path->target_override_active = false;
                    path->completed = true;
                    drone.queue.clear();
                    drone.active_array_id.clear();
                    finish_array_if_done_locked(array_it->second);
                    return;
                }

                if (!drone.queue.empty()) {
                    drone.queue.pop_front();
                }
                path->current_index += 1;

                if (path->current_index >= path->plan.size()) {
                    if ((path->mode == "recon" || path->mode == "patrol") && path->closed_loop) {
                        path->current_index = 0;
                        drone.queue.clear();
                        for (const auto& command : path->plan) {
                            drone.queue.push_back(command);
                        }
                    } else {
                        path->completed = true;
                        drone.queue.clear();
                        drone.active_array_id.clear();
                        finish_array_if_done_locked(array_it->second);
                    }
                }
                return;
            }
        }

        drone.queue.pop_front();
    }

    void finish_array_if_done_locked(ArrayTask& task) {
        bool all_completed = true;
        for (const auto& path : task.paths) {
            if (!path.completed) {
                all_completed = false;
                break;
            }
        }

        if (all_completed) {
            task.status = ArrayStatus::done;
        }
    }

    std::vector<std::string> stop_array_locked(const std::string& array_id) {
        std::vector<std::string> drones_to_hover;
        auto it = arrays_.find(array_id);
        if (it == arrays_.end()) {
            return drones_to_hover;
        }

        it->second.status = ArrayStatus::stopped;
        for (auto& path : it->second.paths) {
            path.completed = true;
            auto drone_it = drones_.find(path.drone_id);
            if (drone_it == drones_.end()) {
                continue;
            }
            auto& drone = drone_it->second;
            drone.queue.clear();
            drone.active_array_id.clear();
            drone.paused = false;
            if (drone.record.status == DroneStatus::online || drone.record.status == DroneStatus::connecting) {
                drones_to_hover.push_back(path.drone_id);
            }
        }
        return drones_to_hover;
    }

    void broadcast_messages(const std::vector<boost::json::object>& messages) const {
        if (ws_manager_ == nullptr) {
            return;
        }
        for (const auto& message : messages) {
            ws_manager_->broadcast(json_stringify(message));
        }
    }

    std::optional<PendingPacket> build_packet_for_drone_locked(const std::string& id, bool heartbeat_tick) {
        auto it = drones_.find(id);
        if (it == drones_.end()) {
            return std::nullopt;
        }

        auto& drone = it->second;
        if (drone.record.status != DroneStatus::online && drone.record.status != DroneStatus::connecting) {
            return std::nullopt;
        }

        const int send_port = config_.send_port_for_slot(drone.record.slot);
        if (send_port == 0 || drone.record.ip.empty()) {
            return std::nullopt;
        }

        PendingPacket packet;
        packet.drone_id = id;
        packet.ip = drone.record.ip;
        packet.send_port = send_port;
        packet.heartbeat_tick = heartbeat_tick;

        if (!drone.paused && !drone.queue.empty()) {
            packet.x = static_cast<float>(drone.queue.front().ned.x);
            packet.y = static_cast<float>(drone.queue.front().ned.y);
            packet.z = static_cast<float>(drone.queue.front().ned.z);
            packet.mode = drone.queue.front().mode;
        } else {
            packet.mode = 0;
        }

        if (heartbeat_tick) {
            const auto now = std::chrono::steady_clock::now();
            if (!drone.first_heartbeat_at.has_value()) {
                drone.first_heartbeat_at = now;
            }
            drone.last_heartbeat_at = now;
            drone.heartbeat_count += 1;
        }

        return packet;
    }

    void dispatch_current_packet(const std::string& id, bool heartbeat_tick) {
        std::optional<PendingPacket> packet;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            packet = build_packet_for_drone_locked(id, heartbeat_tick);
        }

        if (!packet.has_value()) {
            return;
        }

        send_control_packet(
            packet->ip,
            packet->send_port,
            packet->x,
            packet->y,
            packet->z,
            packet->mode
        );
    }

    void dispatch_hover_packets(const std::vector<std::string>& drone_ids) {
        for (const auto& id : drone_ids) {
            dispatch_current_packet(id, false);
        }
    }

    void heartbeat_loop() {
        const auto interval = std::chrono::milliseconds(
            static_cast<int>(std::round(1000.0 / config_.heartbeat_hz))
        );

        while (running_.load()) {
            std::vector<PendingPacket> packets;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& item : drones_) {
                    auto packet = build_packet_for_drone_locked(item.first, true);
                    if (packet.has_value()) {
                        packets.push_back(*packet);
                    }
                }
            }

            for (const auto& packet : packets) {
                send_control_packet(packet.ip, packet.send_port, packet.x, packet.y, packet.z, packet.mode);
            }

            std::this_thread::sleep_for(interval);
        }
    }

    void timeout_loop() {
        while (running_.load()) {
            std::vector<boost::json::object> outbound;
            std::vector<std::string> hover_targets;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto now = std::chrono::steady_clock::now();

                for (auto& item : drones_) {
                    auto& drone = item.second;
                    if ((drone.record.status == DroneStatus::online || drone.record.status == DroneStatus::connecting) &&
                        drone.last_telemetry_at.has_value()) {
                        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            now - *drone.last_telemetry_at
                        ).count();
                        if (elapsed >= config_.lost_timeout_sec) {
                            drone.record.status = DroneStatus::lost;
                            if (!drone.lost_alerted) {
                                outbound.push_back(boost::json::object{
                                    {"type", "event"},
                                    {"drone_id", item.first},
                                    {"event", "lost_connection"},
                                });
                                outbound.push_back(boost::json::object{
                                    {"type", "alert"},
                                    {"drone_id", item.first},
                                    {"alert", "lost_connection"},
                                });
                                drone.lost_alerted = true;
                            }
                        }
                    }
                }

                for (auto& item : arrays_) {
                    auto& task = item.second;
                    if (task.status != ArrayStatus::assembling || task.timeout_emitted) {
                        continue;
                    }

                    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        now - task.created_at
                    ).count();
                    if (elapsed < config_.assembly_timeout_sec) {
                        continue;
                    }

                    task.status = ArrayStatus::timeout;
                    task.timeout_emitted = true;
                    outbound.push_back(boost::json::object{
                        {"type", "assembly_timeout"},
                        {"array_id", task.array_id},
                        {"ready_count", static_cast<std::int64_t>(task.ready_count)},
                        {"total_count", static_cast<std::int64_t>(task.total_count)},
                    });

                    for (auto& path : task.paths) {
                        auto& drone = require_drone_locked(path.drone_id);
                        drone.queue.clear();
                        drone.active_array_id.clear();
                        hover_targets.push_back(path.drone_id);
                    }
                }
            }

            broadcast_messages(outbound);
            dispatch_hover_packets(hover_targets);

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    Config config_;
    WsManager* ws_manager_ = nullptr;
    Logger* logger_ = nullptr;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, DroneRuntime> drones_;
    std::unordered_map<std::string, ArrayTask> arrays_;
    int next_drone_id_ = 1;
    int next_array_id_ = 1;

    std::atomic<bool> running_{false};
    std::thread heartbeat_thread_;
    std::thread timeout_thread_;
};
