#include "http/http_server.h"
#include "conversion/assignment_solver.h"
#include "conversion/coordinate_converter.h"
#include "conversion/quaternion_utils.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {

boost::json::value get_number_or_string_id(int drone_id)
{
    return drone_id;
}

std::string drone_id_string(int drone_id)
{
    return "d" + std::to_string(drone_id);
}

std::string value_to_string(const boost::json::value& value)
{
    if (value.is_string()) return std::string(value.as_string());
    if (value.is_int64()) return std::to_string(value.as_int64());
    if (value.is_uint64()) return std::to_string(value.as_uint64());
    if (value.is_double()) return std::to_string(static_cast<int>(value.as_double()));
    return "";
}

double get_number(const boost::json::object& obj, const char* key, double fallback = 0.0)
{
    if (!obj.contains(key)) return fallback;
    const auto& value = obj.at(key);
    if (value.is_double()) return value.as_double();
    if (value.is_int64()) return static_cast<double>(value.as_int64());
    if (value.is_uint64()) return static_cast<double>(value.as_uint64());
    return fallback;
}

int get_int(const boost::json::object& obj, const char* key, int fallback = 0)
{
    return static_cast<int>(get_number(obj, key, fallback));
}

bool get_bool(const boost::json::object& obj, const char* key, bool fallback = false)
{
    if (!obj.contains(key)) return fallback;
    const auto& value = obj.at(key);
    if (value.is_bool()) return value.as_bool();
    return fallback;
}

std::string get_string(const boost::json::object& obj, const char* key,
                       const std::string& fallback = "")
{
    if (!obj.contains(key)) return fallback;
    auto value = value_to_string(obj.at(key));
    return value.empty() ? fallback : value;
}

std::string normalize_command_mode(const std::string& raw, const std::string& fallback = "move")
{
    std::string mode = raw.empty() ? fallback : raw;
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (mode == "recon") return "scout";
    return mode;
}

bool is_target_command_mode(const std::string& mode)
{
    return mode == "move" || mode == "scout" || mode == "patrol" || mode == "attack";
}

bool is_array_mode(const std::string& mode)
{
    return mode == "scout" || mode == "patrol" || mode == "attack";
}

double current_unix_seconds()
{
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())
        / 1000000.0;
}

} // namespace

// ============================================================
// 构造 / 析构
// ============================================================
HttpServer::HttpServer(const AppConfig& config,
                       DroneManager& drone_mgr,
                       AssemblyController& assembly_ctrl,
                       ExecutionEngine& exec_engine,
                       WsManager& ws_manager)
    : config_(config)
    , drone_mgr_(drone_mgr)
    , assembly_ctrl_(assembly_ctrl)
    , exec_engine_(exec_engine)
    , ws_manager_(ws_manager)
{
    // 注册 DroneManager 回调 -> WS 推送
    drone_mgr_.SetTelemetryCallback([this](int drone_id, const TelemetryData& tel) {
        // 构建遥测 JSON
        double ux, uy, uz;
        CoordinateConverter::NedToUeOffset(
            tel.position_ned[0], tel.position_ned[1], tel.position_ned[2],
            ux, uy, uz);

        auto msg = boost::json::object{
            {"type",     "telemetry"},
            {"drone_id", get_number_or_string_id(drone_id)},
            {"drone_id_str", drone_id_string(drone_id)},
            {"x",        ux},
            {"y",        uy},
            {"z",        uz},
            {"yaw",      0.0},
            {"pitch",    0.0},
            {"roll",     0.0},
            {"speed",    0.0},
            {"battery",  tel.battery},
            {"armed",    tel.IsArmed()},
            {"offboard", tel.IsOffboard()},
            {"gps_lat",  tel.gps_lat},
            {"gps_lon",  tel.gps_lon},
            {"gps_alt",  tel.gps_alt},
            {"gps_fix",  tel.gps_fix},
        };
        double roll, pitch, yaw;
        QuaternionUtils::QuatToEuler(
            tel.quaternion[0], tel.quaternion[1], tel.quaternion[2], tel.quaternion[3],
            roll, pitch, yaw);
        msg["roll"] = roll;
        msg["pitch"] = pitch;
        msg["yaw"] = yaw;
        msg["speed"] = QuaternionUtils::SpeedFromVelocity(
            tel.velocity[0], tel.velocity[1], tel.velocity[2]);
        ws_manager_.broadcast(json_stringify(msg));
    });

    drone_mgr_.SetStateChangeCallback([this](int drone_id, StateEvent event, const GpsAnchor& anchor) {
        std::string type;
        switch (event) {
            case StateEvent::PowerOn:        type = "power_on";       break;
            case StateEvent::LostConnection: type = "lost_connection"; break;
            case StateEvent::Reconnect:      type = "reconnect";      break;
        }
        auto msg = boost::json::object{
            {"type",     "event"},
            {"drone_id", get_number_or_string_id(drone_id)},
            {"drone_id_str", drone_id_string(drone_id)},
            {"event",    type},
        };
        if (anchor.valid) {
            msg["gps_lat"] = anchor.latitude;
            msg["gps_lon"] = anchor.longitude;
            msg["gps_alt"] = anchor.altitude;
        }
        spdlog::debug("[WS Push] event={} drone={} anchor_valid={}",
                      type, drone_id_string(drone_id), anchor.valid);
        ws_manager_.broadcast(json_stringify(msg));
    });

    drone_mgr_.SetAlertCallback([this](int drone_id, const std::string& alert_type, int value) {
        auto msg = boost::json::object{
            {"type",       "alert"},
            {"drone_id",   get_number_or_string_id(drone_id)},
            {"drone_id_str", drone_id_string(drone_id)},
            {"alert",      alert_type},
            {"alert_type", alert_type},
            {"value",      value},
        };
        spdlog::debug("[WS Push] alert={} drone={} value={}",
                      alert_type, drone_id_string(drone_id), value);
        ws_manager_.broadcast(json_stringify(msg));
    });

    assembly_ctrl_.SetProgressCallback([this](const AssemblyProgress& progress) {
        auto msg = boost::json::object{
            {"type",        "assembling"},
            {"array_id",    progress.array_id},
            {"ready_count", progress.ready_count},
            {"total_count", progress.total_count},
        };
        spdlog::debug("[WS Push] assembling array={} {}/{}",
                      progress.array_id, progress.ready_count, progress.total_count);
        ws_manager_.broadcast(json_stringify(msg));

        if (progress.ready_count >= progress.total_count && progress.total_count > 0) {
            auto done_msg = boost::json::object{
                {"type",     "assembly_complete"},
                {"array_id", progress.array_id},
            };
            spdlog::debug("[WS Push] assembly_complete array={}", progress.array_id);
            ws_manager_.broadcast(json_stringify(done_msg));
            // 集结完成 -> 启动执行引擎
            exec_engine_.StartTasks(assembly_ctrl_.GetConfig());
        }
    });

    assembly_ctrl_.SetTimeoutCallback([this](const AssemblyProgress& progress) {
        auto msg = boost::json::object{
            {"type",        "assembly_timeout"},
            {"array_id",    progress.array_id},
            {"ready_count", progress.ready_count},
            {"total_count", progress.total_count},
        };
        spdlog::debug("[WS Push] assembly_timeout array={} {}/{}",
                      progress.array_id, progress.ready_count, progress.total_count);
        ws_manager_.broadcast(json_stringify(msg));

        for (const auto& path : assembly_ctrl_.GetConfig().paths) {
            int drone_id = 0;
            try {
                std::string raw = path.drone_id;
                if (!raw.empty() && (raw[0] == 'd' || raw[0] == 'D')) raw = raw.substr(1);
                drone_id = std::stoi(raw);
            } catch (...) {
                drone_id = 0;
            }
            if (drone_id <= 0) continue;

            DroneTaskState state;
            state.drone_id = drone_id;
            state.array_id = assembly_ctrl_.GetConfig().array_id;
            state.mode = assembly_ctrl_.GetConfig().mode;
            state.state = "error";
            state.waypoint_count = static_cast<int>(path.waypoints.size());
            state.detail = "assembly_timeout";
            PublishTaskState(state);
        }
    });

    exec_engine_.SetTaskStateCallback([this](const DroneTaskState& state) {
        PublishTaskState(state);
    });

    LoadDrones();
}

HttpServer::~HttpServer()
{
    exec_engine_.SetTaskStateCallback(nullptr);
}

// ============================================================
// Run / Stop
// ============================================================
void HttpServer::Run() {
    running_ = true;
    http_thread_ = std::thread(&HttpServer::RunHttpServer, this);
    ws_thread_   = std::thread(&HttpServer::RunWsServer,   this);
    http_thread_.join();
    ws_thread_.join();
}

void HttpServer::Stop() {
    running_ = false;
    // 等待连接处理线程退出（HTTP 单次请求很快退出，WS 会话在 running_=false 后关闭）
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(conn_threads_mutex_);
        threads = std::move(conn_threads_);
    }
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
}

// ============================================================
// 工具函数
// ============================================================
bool HttpServer::PathMatch(const std::string& path,
                           const std::string& prefix,
                           const std::string& suffix,
                           std::string& id_out)
{
    if (path.size() < prefix.size() ||
        path.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    std::string rest = path.substr(prefix.size());
    if (suffix.empty()) {
        if (rest.empty() || rest.find('/') != std::string::npos) return false;
        id_out = rest;
        return true;
    }
    auto pos = rest.find(suffix);
    if (pos == std::string::npos) return false;
    id_out = rest.substr(0, pos);
    return !id_out.empty() && rest.substr(pos) == suffix;
}

http::response<http::string_body> HttpServer::MakeResponse(
    const http::request<http::string_body>& req,
    int status_code,
    const std::string& body,
    const std::string& content_type)
{
    http::response<http::string_body> res{
        static_cast<http::status>(status_code), req.version()};
    res.set(http::field::server, "UE5DroneControl-Backend/1.0");
    res.set(http::field::content_type, content_type);
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET,POST,PUT,DELETE,OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
    res.keep_alive(req.keep_alive());
    res.body() = body;
    res.prepare_payload();
    return res;
}

static std::string json_error(const std::string& detail) {
    return json_stringify(boost::json::object{{"detail", detail}});
}

static boost::json::value parse_body(const http::request<http::string_body>& req) {
    if (req.body().empty()) return boost::json::object{};
    try {
        return boost::json::parse(req.body());
    } catch (...) {
        throw ApiError(400, "invalid JSON body");
    }
}

// ============================================================
// HTTP 路由
// ============================================================
http::response<http::string_body> HttpServer::HandleHttp(
    http::request<http::string_body>& req)
{
    try {
        std::string method = std::string(req.method_string());
        std::string path   = std::string(req.target());
        auto qpos = path.find('?');
        if (qpos != std::string::npos) path = path.substr(0, qpos);

        if (method == "OPTIONS") return MakeResponse(req, 204, "");

        const auto body = parse_body(req);
        std::string id;

        if (method == "GET" && path == "/")
            return MakeResponse(req, 200, json_stringify(boost::json::object{
                {"status", "ok"},
                {"http_port", config_.http_port},
                {"ws_port",   config_.ws_port},
                {"debug",     config_.debug},
            }));

        if (method == "GET"  && path == "/api/drones")
            return MakeResponse(req, 200, json_stringify(ApiListDrones()));
        if (method == "POST" && path == "/api/drones")
            return MakeResponse(req, 201, json_stringify(ApiRegisterDrone(require_object(body, "body"))));
        if (method == "PUT"  && PathMatch(path, "/api/drones/", "", id))
            return MakeResponse(req, 200, json_stringify(ApiUpdateDrone(id, require_object(body, "body"))));
        if (method == "DELETE" && PathMatch(path, "/api/drones/", "", id))
            return MakeResponse(req, 200, json_stringify(ApiDeleteDrone(id)));
        if (method == "GET"  && PathMatch(path, "/api/drones/", "/anchor", id))
            return MakeResponse(req, 200, json_stringify(ApiGetAnchor(id)));
        if (method == "POST" && path == "/api/drones/refresh")
            return MakeResponse(req, 200, json_stringify(ApiRefreshDrones()));
        if (method == "POST" && path == "/api/arrays")
            return MakeResponse(req, 201, json_stringify(ApiCreateArray(require_object(body, "body"))));
        if (method == "POST" && PathMatch(path, "/api/arrays/", "/stop", id))
            return MakeResponse(req, 200, json_stringify(ApiStopArray(id)));

        if (!config_.debug) throw ApiError(404, "not found");

        if (method == "GET"  && PathMatch(path, "/api/debug/drone/", "/state", id))
            return MakeResponse(req, 200, json_stringify(DebugDroneState(id)));
        if (method == "GET"  && PathMatch(path, "/api/debug/drone/", "/queue", id))
            return MakeResponse(req, 200, json_stringify(DebugDroneQueue(id)));
        if (method == "GET"  && PathMatch(path, "/api/debug/heartbeat/", "", id))
            return MakeResponse(req, 200, json_stringify(DebugHeartbeat(id)));
        if (method == "POST" && PathMatch(path, "/api/debug/drone/", "/inject", id))
            return MakeResponse(req, 200, json_stringify(DebugInjectTelemetry(id, require_object(body, "body"))));
        if (method == "POST" && PathMatch(path, "/api/debug/cmd/", "/move", id))
            return MakeResponse(req, 200, json_stringify(DebugMove(id, require_object(body, "body"))));
        if (method == "POST" && PathMatch(path, "/api/debug/cmd/", "/pause", id))
            return MakeResponse(req, 200, json_stringify(DebugPause(id, true)));
        if (method == "POST" && PathMatch(path, "/api/debug/cmd/", "/resume", id))
            return MakeResponse(req, 200, json_stringify(DebugPause(id, false)));
        if (method == "POST" && path == "/api/debug/cmd/batch/array")
            return MakeResponse(req, 200, json_stringify(DebugBatchArray(require_array(body, "body"))));
        if (method == "POST" && PathMatch(path, "/api/debug/cmd/", "/array", id))
            return MakeResponse(req, 200, json_stringify(DebugSingleArray(id, require_object(body, "body"))));
        if (method == "POST" && PathMatch(path, "/api/debug/cmd/", "/target", id))
            return MakeResponse(req, 200, json_stringify(DebugTarget(id, require_object(body, "body"))));
        if (method == "GET"  && PathMatch(path, "/api/debug/arrays/", "/state", id))
            return MakeResponse(req, 200, json_stringify(DebugArrayState(id)));

        throw ApiError(404, "not found");
    } catch (const ApiError& e) {
        return MakeResponse(req, e.status_code, json_error(e.what()));
    } catch (const std::exception& e) {
        spdlog::error("[HTTP] handler exception: {}", e.what());
        return MakeResponse(req, 500, json_error("internal server error"));
    }
}

// ============================================================
// REST 路由实现
// ============================================================
static int drone_id_from_string(const std::string& id) {
    // "d1" -> 1, "1" -> 1
    if (!id.empty() && id[0] == 'd') {
        try { return std::stoi(id.substr(1)); } catch (...) {}
    }
    try { return std::stoi(id); } catch (...) {}
    throw ApiError(400, "invalid drone id: " + id);
}

boost::json::value HttpServer::ApiListDrones() {
    std::lock_guard<std::mutex> lock(records_mutex_);
    boost::json::array arr;
    for (const auto& rec : drone_records_) {
        int numeric_id = drone_id_from_string(rec.id);
        auto status = drone_mgr_.GetStatus(numeric_id);
        const auto telemetry = drone_mgr_.GetLatestTelemetry(numeric_id);
        auto mapping = config_.port_map.find(rec.slot);
        const int recv_port = mapping != config_.port_map.end() ? mapping->second.recv_port : 0;
        const std::string topic_prefix = mapping != config_.port_map.end()
            ? mapping->second.ros_topic_prefix
            : "/px4_" + std::to_string(rec.slot);
        DroneTaskState task_state;
        {
            std::lock_guard<std::mutex> task_lock(task_state_mutex_);
            auto task_it = task_states_.find(numeric_id);
            if (task_it != task_states_.end()) {
                task_state = task_it->second;
            } else {
                task_state.drone_id = numeric_id;
            }
        }

        arr.push_back(boost::json::object{
            {"id", numeric_id},
            {"id_str", rec.id},
            {"name", rec.name},
            {"model", rec.model},
            {"slot", rec.slot},
            {"slot_number", rec.slot},
            {"ip", rec.ip},
            {"port", rec.port},
            {"video_url", rec.video_url},
            {"status", status.connection_state},
            {"battery", status.battery},
            {"x", status.pos_x},
            {"y", status.pos_y},
            {"z", status.pos_z},
            {"yaw", status.yaw},
            {"speed", status.speed},
            {"gps_lat", telemetry.gps_lat},
            {"gps_lon", telemetry.gps_lon},
            {"gps_alt", telemetry.gps_alt},
            {"gps_fix", telemetry.gps_fix},
            {"armed", telemetry.IsArmed()},
            {"offboard", telemetry.IsOffboard()},
            {"task_mode", task_state.mode},
            {"task_state", task_state.state},
            {"task_array_id", task_state.array_id},
            {"task_current_wp", task_state.current_wp},
            {"task_waypoint_count", task_state.waypoint_count},
            {"task_detail", task_state.detail},
            {"task_updated_at", task_state.updated_at},
            {"ue_receive_port", recv_port},
            {"topic_prefix", topic_prefix},
            {"bit_index", rec.slot > 0 ? rec.slot - 1 : 0},
            {"mavlink_system_id", rec.slot},
        });
    }
    return arr;
}

boost::json::value HttpServer::ApiRefreshDrones()
{
    const auto refreshed_ids = drone_mgr_.RefreshDisconnectedConnections();
    boost::json::array ids;
    for (const int id : refreshed_ids) {
        ids.push_back(id);
    }
    spdlog::info("[HTTP] Connection refresh requested; probing {} disconnected drone(s)",
                 refreshed_ids.size());
    return boost::json::object{
        {"ok", true},
        {"message", "safe hold probes sent; a drone becomes online only after telemetry is received"},
        {"refreshed_drone_ids", std::move(ids)},
    };
}

boost::json::value HttpServer::ApiRegisterDrone(const boost::json::object& body) {
    std::lock_guard<std::mutex> lock(records_mutex_);

    if (static_cast<int>(drone_records_.size()) >= config_.max_count)
        throw ApiError(507, "max drone count reached");

    DroneRecord rec;
    rec.id        = "d" + std::to_string(next_drone_seq_++);
    rec.name      = get_string(body, "name", rec.id);
    rec.model     = get_string(body, "model", "");
    rec.slot      = body.contains("slot") ? get_int(body, "slot")
                                          : get_int(body, "slot_number", next_drone_seq_ - 1);
    rec.ip        = get_string(body, "ip", config_.jetson_host);
    rec.port      = get_int(body, "port", 0);
    rec.video_url = get_string(body, "video_url", "");

    int drone_id = drone_id_from_string(rec.id);

    // 配置 Jetson 目标地址
    auto it = config_.port_map.find(rec.slot);
    if (it == config_.port_map.end())
        throw ApiError(400, "slot " + std::to_string(rec.slot) + " not in port_map");

    for (const auto& existing : drone_records_) {
        if (existing.name == rec.name) {
            throw ApiError(409, "drone name already exists: " + rec.name);
        }
        if (existing.slot == rec.slot) {
            throw ApiError(409, "slot already exists: " + std::to_string(rec.slot));
        }
    }

    rec.port = rec.port > 0 ? rec.port : it->second.send_port;
    if (!drone_mgr_.AddDrone(drone_id, rec.slot, rec.name,
                             config_.jetson_host, it->second.send_port)) {
        throw ApiError(409, "drone registration conflict");
    }
    drone_records_.push_back(rec);
    SaveDrones();

    spdlog::info("[HTTP] Registered drone {} slot={}", rec.id, rec.slot);
    return boost::json::object{{"id", drone_id}, {"id_str", rec.id}, {"slot", rec.slot}, {"name", rec.name}};
}

boost::json::value HttpServer::ApiUpdateDrone(const std::string& id, const boost::json::object& body) {
    std::lock_guard<std::mutex> lock(records_mutex_);
    const int numeric_id = drone_id_from_string(id);
    const std::string canonical_id = drone_id_string(numeric_id);
    for (auto& rec : drone_records_) {
        if (rec.id != canonical_id) continue;
        if (body.contains("name"))      rec.name      = get_string(body, "name", rec.name);
        if (body.contains("model"))     rec.model     = get_string(body, "model", rec.model);
        if (body.contains("ip"))        rec.ip        = get_string(body, "ip", rec.ip);
        if (body.contains("port"))      rec.port      = get_int(body, "port", rec.port);
        if (body.contains("video_url")) rec.video_url = get_string(body, "video_url", rec.video_url);
        SaveDrones();
        return boost::json::object{{"id", numeric_id}, {"id_str", rec.id}, {"updated", true}, {"name", rec.name}};
    }
    throw ApiError(404, "drone not found: " + id);
}

boost::json::value HttpServer::ApiDeleteDrone(const std::string& id) {
    std::lock_guard<std::mutex> lock(records_mutex_);
    const int numeric_id = drone_id_from_string(id);
    const std::string canonical_id = drone_id_string(numeric_id);
    for (auto it = drone_records_.begin(); it != drone_records_.end(); ++it) {
        if (it->id != canonical_id) continue;
        drone_mgr_.RemoveDrone(numeric_id);
        drone_records_.erase(it);
        SaveDrones();
        return boost::json::object{{"id", numeric_id}, {"id_str", canonical_id}, {"deleted", true}};
    }
    throw ApiError(404, "drone not found: " + id);
}

boost::json::value HttpServer::ApiGetAnchor(const std::string& id) {
    int drone_id = drone_id_from_string(id);
    auto anchor = drone_mgr_.GetAnchor(drone_id);
    if (!anchor.valid) {
        throw ApiError(425, "anchor not available");
    }
    return boost::json::object{
        {"drone_id",  drone_id},
        {"drone_id_str", drone_id_string(drone_id)},
        {"gps_lat",   anchor.latitude},
        {"gps_lon",   anchor.longitude},
        {"gps_alt",   anchor.altitude},
        {"valid",     anchor.valid},
    };
}

boost::json::value HttpServer::ApiCreateArray(const boost::json::object& body) {
    AssemblyConfig cfg;
    cfg.array_id = body.contains("array_id") ? std::string(body.at("array_id").as_string()) : "a1";
    cfg.mode     = normalize_command_mode(get_string(body, "mode", "scout"), "scout");
    if (!is_array_mode(cfg.mode)) {
        throw ApiError(400, "invalid mode: " + cfg.mode);
    }

    if (body.contains("paths") && body.at("paths").is_array()) {
        for (const auto& p : body.at("paths").as_array()) {
            if (!p.is_object()) continue;
            const auto& po = p.as_object();
            AssemblyConfig::Path path;
            path.path_id = po.contains("pathId") ? get_int(po, "pathId")
                                                 : get_int(po, "path_id", 0);
            path.drone_id = get_string(po, "drone_id", "");
            if (path.drone_id.empty() && po.contains("droneId")) {
                path.drone_id = get_string(po, "droneId", "");
            }
            path.closed_loop = get_bool(po, "bClosedLoop", false) ||
                               get_bool(po, "closed_loop", false);

            if (po.contains("waypoints") && po.at("waypoints").is_array()) {
                for (const auto& w : po.at("waypoints").as_array()) {
                    if (!w.is_object()) continue;
                    const auto& wo = w.as_object();
                    AssemblyConfig::Path::Waypoint wp;
                    const boost::json::object* loc = &wo;
                    if (wo.contains("location") && wo.at("location").is_object()) {
                        loc = &wo.at("location").as_object();
                    }
                    wp.x = get_number(*loc, "x", 0.0);
                    wp.y = get_number(*loc, "y", 0.0);
                    wp.z = get_number(*loc, "z", 0.0);
                    wp.segment_speed = static_cast<float>(
                        wo.contains("segmentSpeed") ? get_number(wo, "segmentSpeed", 0.0)
                                                    : get_number(wo, "segment_speed", 0.0));
                    wp.wait_time = static_cast<float>(
                        wo.contains("waitTime") ? get_number(wo, "waitTime", 0.0)
                                                : get_number(wo, "wait_time", 0.0));
                    path.waypoints.push_back(wp);
                }
            }
            cfg.paths.push_back(path);
        }
    }

    if (get_bool(body, "auto_assign", false)) {
        if (assembly_ctrl_.GetState() != AssemblyState::Idle)
            throw ApiError(409, "assembly already in progress");
        AutoAssignDrones(cfg);
    }

    if (!assembly_ctrl_.Start(cfg, config_.assembly_safety_cylinder_m))
        throw ApiError(409, "assembly already in progress");

    for (const auto& path : cfg.paths) {
        int drone_id = 0;
        try { drone_id = drone_id_from_string(path.drone_id); } catch (...) { drone_id = 0; }
        if (drone_id <= 0) continue;

        DroneTaskState state;
        state.drone_id = drone_id;
        state.array_id = cfg.array_id;
        state.mode = cfg.mode;
        state.state = "assembling";
        state.waypoint_count = static_cast<int>(path.waypoints.size());
        PublishTaskState(state);
    }

    return boost::json::object{{"array_id", cfg.array_id}, {"status", "assembling"}};
}

void HttpServer::AutoAssignDrones(AssemblyConfig& cfg) {
    // 收集有航点的路径，提取首航点并转换为 NED
    std::vector<size_t> path_indices;
    std::vector<std::array<double, 3>> path_ned;
    for (size_t i = 0; i < cfg.paths.size(); ++i) {
        if (cfg.paths[i].waypoints.empty()) continue;
        const auto& wp = cfg.paths[i].waypoints[0];
        double ned_n, ned_e, ned_d;
        CoordinateConverter::UeOffsetToNed(wp.x, wp.y, wp.z, ned_n, ned_e, ned_d);
        path_indices.push_back(i);
        path_ned.push_back({ned_n, ned_e, ned_d});
    }
    if (path_indices.empty())
        throw ApiError(400, "auto_assign requires at least one path with waypoints");

    // 收集在线无人机及其当前 NED 位置
    std::vector<int> drone_ids;
    std::vector<std::array<double, 3>> drone_pos;
    {
        std::lock_guard<std::mutex> lock(records_mutex_);
        for (const auto& rec : drone_records_) {
            int id = 0;
            try { id = drone_id_from_string(rec.id); } catch (...) { continue; }
            if (drone_mgr_.GetConnectionState(id) != DroneConnectionState::Online) continue;
            const auto tel = drone_mgr_.GetLatestTelemetry(id);
            drone_ids.push_back(id);
            drone_pos.push_back({tel.position_ned[0], tel.position_ned[1], tel.position_ned[2]});
        }
    }
    if (drone_ids.size() < path_indices.size()) {
        throw ApiError(409, "auto_assign: online drones (" + std::to_string(drone_ids.size()) +
                            ") fewer than paths (" + std::to_string(path_indices.size()) + ")");
    }

    // cost[i][j] = 无人机 i 当前位置到路径 j 首航点的欧氏距离
    std::vector<std::vector<double>> cost(
        drone_ids.size(), std::vector<double>(path_indices.size()));
    for (size_t i = 0; i < drone_ids.size(); ++i) {
        for (size_t j = 0; j < path_indices.size(); ++j) {
            const double dx = drone_pos[i][0] - path_ned[j][0];
            const double dy = drone_pos[i][1] - path_ned[j][1];
            const double dz = drone_pos[i][2] - path_ned[j][2];
            cost[i][j] = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }

    const auto assignment = AssignmentSolver::HungarianMinCost(cost);

    boost::json::array assignments;
    for (size_t i = 0; i < drone_ids.size() && i < assignment.size(); ++i) {
        const int j = assignment[i];
        if (j < 0 || j >= static_cast<int>(path_indices.size())) continue;
        auto& path = cfg.paths[path_indices[j]];
        path.drone_id = drone_id_string(drone_ids[i]);
        assignments.push_back(boost::json::object{
            {"path_id",  path.path_id},
            {"drone_id", path.drone_id},
        });
        spdlog::info("[HTTP] auto_assign: path {} -> {} (dist={:.2f}m)",
                     path.path_id, path.drone_id, cost[i][j]);
    }
    if (assignments.size() != path_indices.size())
        throw ApiError(500, "auto_assign: assignment incomplete");

    ws_manager_.broadcast(json_stringify(boost::json::object{
        {"type",        "assignment_result"},
        {"array_id",    cfg.array_id},
        {"assignments", assignments},
    }));
    spdlog::info("[HTTP] auto_assign: {} paths assigned for array '{}'",
                 assignments.size(), cfg.array_id);
}

boost::json::value HttpServer::ApiStopArray(const std::string& id) {
    const AssemblyConfig config = assembly_ctrl_.GetConfig();
    exec_engine_.StopAll();
    assembly_ctrl_.Stop();
    for (const auto& path : config.paths) {
        int drone_id = 0;
        try { drone_id = drone_id_from_string(path.drone_id); } catch (...) { drone_id = 0; }
        if (drone_id <= 0) continue;

        DroneTaskState state;
        state.drone_id = drone_id;
        state.array_id = config.array_id;
        state.mode = config.mode;
        state.state = "standby";
        state.waypoint_count = static_cast<int>(path.waypoints.size());
        state.detail = "array_stopped";
        PublishTaskState(state);
    }
    return boost::json::object{{"array_id", id}, {"status", "stopped"}};
}

void HttpServer::PublishTaskState(const DroneTaskState& input)
{
    if (input.drone_id <= 0) return;

    DroneTaskState state = input;
    if (state.updated_at <= 0.0) state.updated_at = current_unix_seconds();

    {
        std::lock_guard<std::mutex> lock(task_state_mutex_);
        task_states_[state.drone_id] = state;
    }

    ws_manager_.broadcast(json_stringify(boost::json::object{
        {"type", "drone_task_state"},
        {"drone_id", state.drone_id},
        {"drone_id_str", drone_id_string(state.drone_id)},
        {"array_id", state.array_id},
        {"mode", state.mode},
        {"state", state.state},
        {"current_wp", state.current_wp},
        {"waypoint_count", state.waypoint_count},
        {"detail", state.detail},
        {"updated_at", state.updated_at},
    }));
}

// ============================================================
// Debug 路由
// ============================================================
boost::json::value HttpServer::DebugDroneState(const std::string& id) {
    int drone_id = drone_id_from_string(id);
    if (!drone_mgr_.HasDrone(drone_id)) throw ApiError(404, "drone not found: " + id);
    auto status = drone_mgr_.GetStatus(drone_id);
    auto hb = drone_mgr_.GetHeartbeatStats(drone_id);
    return boost::json::object{
        {"id",     drone_id},
        {"id_str", drone_id_string(drone_id)},
        {"slot",   status.slot},
        {"status", status.connection_state},
        {"battery", status.battery},
        {"x", status.pos_x}, {"y", status.pos_y}, {"z", status.pos_z},
        {"yaw", status.yaw}, {"speed", status.speed},
        {"queue_size", static_cast<int64_t>(drone_mgr_.GetCommandQueueSize(drone_id))},
        {"queue_paused", drone_mgr_.IsCommandQueuePaused(drone_id)},
        {"heartbeat_running", hb.running},
        {"last_heartbeat_time", hb.last_sent_time},
        {"heartbeat_count", static_cast<int64_t>(hb.sent_count)},
        {"anchor_valid", status.anchor.valid},
        {"gps_lat", status.anchor.latitude},
        {"gps_lon", status.anchor.longitude},
        {"gps_alt", status.anchor.altitude},
        {"control_ack_command_id", status.control_ack_command_id},
        {"control_ack_sequence", static_cast<int64_t>(status.control_ack_sequence)},
    };
}

boost::json::value HttpServer::DebugDroneQueue(const std::string& id) {
    int drone_id = drone_id_from_string(id);
    if (!drone_mgr_.HasDrone(drone_id)) throw ApiError(404, "drone not found: " + id);
    boost::json::array items;
    for (const auto& cmd : drone_mgr_.GetCommandQueueSnapshot(drone_id)) {
        items.push_back(boost::json::object{
            {"sequence", static_cast<int64_t>(cmd.sequence)},
            {"timestamp", cmd.timestamp},
            {"slot", cmd.slot},
            {"x", cmd.x},
            {"y", cmd.y},
            {"z", cmd.z},
            {"mode", cmd.mode},
            {"coordinate_frame", "NED"},
            {"reference", "power_on_origin"},
            {"unit", "m"},
        });
    }
    return boost::json::object{
        {"id", drone_id},
        {"id_str", drone_id_string(drone_id)},
        {"queue_size", static_cast<int64_t>(items.size())},
        {"paused", drone_mgr_.IsCommandQueuePaused(drone_id)},
        {"commands", items},
    };
}

boost::json::value HttpServer::DebugHeartbeat(const std::string& id) {
    int drone_id = drone_id_from_string(id);
    if (!drone_mgr_.HasDrone(drone_id)) throw ApiError(404, "drone not found: " + id);
    auto hb = drone_mgr_.GetHeartbeatStats(drone_id);
    return boost::json::object{
        {"id", drone_id},
        {"id_str", drone_id_string(drone_id)},
        {"running", hb.running},
        {"last_sent_time", hb.last_sent_time},
        {"sent_count", static_cast<int64_t>(hb.sent_count)},
        {"send_failed_count", static_cast<int64_t>(hb.send_failed_count)},
        {"last_ned_x", hb.last_ned_x},
        {"last_ned_y", hb.last_ned_y},
        {"last_ned_z", hb.last_ned_z},
        {"active_sequence", static_cast<int64_t>(hb.active_sequence)},
        {"active_mode", hb.active_mode},
        {"repeat_index", hb.repeat_index},
        {"repeat_total", hb.repeat_total},
    };
}

boost::json::value HttpServer::DebugInjectTelemetry(const std::string& id, const boost::json::object& body) {
    int drone_id = drone_id_from_string(id);
    if (!drone_mgr_.HasDrone(drone_id)) throw ApiError(404, "drone not found: " + id);
    TelemetryData tel{};
    if (body.contains("position") && body.at("position").is_array()) {
        const auto& pos = body.at("position").as_array();
        if (pos.size() >= 3) {
            tel.position_ned[0] = boost::json::value_to<double>(pos[0]);
            tel.position_ned[1] = boost::json::value_to<double>(pos[1]);
            tel.position_ned[2] = boost::json::value_to<double>(pos[2]);
        }
    }
    if (body.contains("q") && body.at("q").is_array()) {
        const auto& q = body.at("q").as_array();
        if (q.size() >= 4) {
            tel.quaternion[0] = boost::json::value_to<double>(q[0]);
            tel.quaternion[1] = boost::json::value_to<double>(q[1]);
            tel.quaternion[2] = boost::json::value_to<double>(q[2]);
            tel.quaternion[3] = boost::json::value_to<double>(q[3]);
        }
    }
    if (body.contains("velocity") && body.at("velocity").is_array()) {
        const auto& vel = body.at("velocity").as_array();
        if (vel.size() >= 3) {
            tel.velocity[0] = boost::json::value_to<double>(vel[0]);
            tel.velocity[1] = boost::json::value_to<double>(vel[1]);
            tel.velocity[2] = boost::json::value_to<double>(vel[2]);
        }
    }
    tel.battery = get_int(body, "battery", -1);
    tel.gps_lat = get_number(body, "gps_lat", 0.0);
    tel.gps_lon = get_number(body, "gps_lon", 0.0);
    tel.gps_alt = get_number(body, "gps_alt", 0.0);
    tel.gps_fix = (body.contains("gps_fix") && body.at("gps_fix").is_bool())
        ? body.at("gps_fix").as_bool()
        : (body.contains("gps_lat") && body.contains("gps_lon") && body.contains("gps_alt"));
    tel.arming_state = static_cast<uint8_t>(get_int(body, "arming_state", 0));
    tel.nav_state = static_cast<uint8_t>(get_int(body, "nav_state", 0));
    drone_mgr_.OnTelemetryReceived(drone_id, tel);
    if (assembly_ctrl_.GetState() == AssemblyState::Assembling) {
        assembly_ctrl_.UpdateDronePosition(
            drone_id,
            tel.position_ned[0],
            tel.position_ned[1],
            tel.position_ned[2]);
    }
    return boost::json::object{{"injected", id}};
}

boost::json::value HttpServer::DebugMove(const std::string& id, const boost::json::object& body) {
    int drone_id = drone_id_from_string(id);
    double x = get_number(body, "x", 0.0);
    double y = get_number(body, "y", 0.0);
    double z = get_number(body, "z", 0.0);
    if (!drone_mgr_.ProcessMoveCommand(drone_id, x, y, z))
        throw ApiError(404, "drone not found: " + id);
    return boost::json::object{{"moved", id}, {"x", x}, {"y", y}, {"z", z}};
}

boost::json::value HttpServer::DebugPause(const std::string& id, bool pause) {
    int drone_id = drone_id_from_string(id);
    bool ok = pause ? drone_mgr_.ProcessPauseCommand(drone_id)
                    : drone_mgr_.ProcessResumeCommand(drone_id);
    if (!ok) throw ApiError(404, "drone not found: " + id);
    return boost::json::object{{"id", id}, {"paused", pause}};
}

boost::json::value HttpServer::DebugSingleArray(const std::string& id, const boost::json::object& body) {
    return ApiCreateArray(body);
}

boost::json::value HttpServer::DebugTarget(const std::string& id, const boost::json::object& body) {
    int drone_id = drone_id_from_string(id);
    if (!drone_mgr_.HasDrone(drone_id)) throw ApiError(404, "drone not found: " + id);
    double x = get_number(body, "x", 0.0);
    double y = get_number(body, "y", 0.0);
    double z = get_number(body, "z", 0.0);
        exec_engine_.InjectTarget(drone_id, x, y, z);
    return boost::json::object{{"target_injected", id}, {"x", x}, {"y", y}, {"z", z}};
}

boost::json::value HttpServer::DebugBatchArray(const boost::json::array& body) {
    AssemblyConfig cfg;
    cfg.array_id = "debug_batch";
    cfg.mode = "scout";

    int path_id = 1;
    for (const auto& item : body) {
        if (!item.is_object()) continue;
        const auto& obj = item.as_object();

        AssemblyConfig::Path path;
        path.path_id = obj.contains("pathId") ? get_int(obj, "pathId")
                                              : get_int(obj, "path_id", path_id++);
        path.drone_id = get_string(obj, "drone_id", "");
        path.closed_loop = get_bool(obj, "bClosedLoop", false) ||
                           get_bool(obj, "closed_loop", false);
        if (obj.contains("mode")) {
            cfg.mode = normalize_command_mode(get_string(obj, "mode", cfg.mode), cfg.mode);
        }

        if (obj.contains("waypoints") && obj.at("waypoints").is_array()) {
            for (const auto& wp_value : obj.at("waypoints").as_array()) {
                if (!wp_value.is_object()) continue;
                const auto& wp_obj = wp_value.as_object();
                const boost::json::object* loc = &wp_obj;
                if (wp_obj.contains("location") && wp_obj.at("location").is_object()) {
                    loc = &wp_obj.at("location").as_object();
                }

                AssemblyConfig::Path::Waypoint wp;
                wp.x = get_number(*loc, "x", 0.0);
                wp.y = get_number(*loc, "y", 0.0);
                wp.z = get_number(*loc, "z", 0.0);
                wp.segment_speed = static_cast<float>(
                    wp_obj.contains("segmentSpeed") ? get_number(wp_obj, "segmentSpeed", 0.0)
                                                     : get_number(wp_obj, "segment_speed", 0.0));
                wp.wait_time = static_cast<float>(
                    wp_obj.contains("waitTime") ? get_number(wp_obj, "waitTime", 0.0)
                                                : get_number(wp_obj, "wait_time", 0.0));
                path.waypoints.push_back(wp);
            }
        }

        if (!path.drone_id.empty() && !path.waypoints.empty()) {
            cfg.paths.push_back(path);
        }
    }

    if (cfg.paths.empty()) {
        throw ApiError(400, "batch array requires at least one path with waypoints");
    }
    if (!is_array_mode(cfg.mode)) {
        throw ApiError(400, "invalid mode: " + cfg.mode);
    }
    if (!assembly_ctrl_.Start(cfg, config_.assembly_safety_cylinder_m)) {
        throw ApiError(409, "assembly already in progress");
    }

    return boost::json::object{
        {"array_id", cfg.array_id},
        {"status", "assembling"},
        {"path_count", static_cast<int64_t>(cfg.paths.size())},
    };
}

boost::json::value HttpServer::DebugArrayState(const std::string& id) {
    auto progress = assembly_ctrl_.GetProgress();
    return boost::json::object{
        {"array_id",    progress.array_id},
        {"ready_count", progress.ready_count},
        {"total_count", progress.total_count},
        {"state",       static_cast<int>(assembly_ctrl_.GetState())},
    };
}

// ============================================================
// WebSocket 命令处理
// ============================================================
void HttpServer::HandleWsCommand(const boost::json::object& msg,
                                  const std::shared_ptr<WsSession>& session)
{
    if (msg.contains("mode")) {
        const std::string mode = normalize_command_mode(value_to_string(msg.at("mode")), "move");
        if (!is_target_command_mode(mode)) {
            ws_manager_.send(session, json_stringify(boost::json::object{
                {"type", "error"}, {"code", 400}, {"message", "unknown mode: " + mode}}));
            return;
        }

        std::string drone_id_str = msg.contains("drone_id") ? value_to_string(msg.at("drone_id")) : "";
        double x = get_number(msg, "x", 0.0);
        double y = get_number(msg, "y", 0.0);
        double z = get_number(msg, "z", 0.0);
        int drone_id = drone_id_from_string(drone_id_str);
        if (!drone_mgr_.ProcessMoveCommand(drone_id, x, y, z)) {
            ws_manager_.send(session, json_stringify(boost::json::object{
                {"type", "error"}, {"code", 404}, {"message", "drone not found"}}));
        } else if (msg.contains("request_id")) {
            ws_manager_.send(session, json_stringify(boost::json::object{
                {"type", "command_ack"},
                {"command", mode},
                {"mode", mode},
                {"request_id", value_to_string(msg.at("request_id"))},
                {"drone_id", drone_id},
                {"drone_id_str", drone_id_string(drone_id)},
            }));
        }
        if (drone_mgr_.HasDrone(drone_id)) {
            DroneTaskState state;
            state.drone_id = drone_id;
            state.mode = mode;
            state.state = mode == "attack" ? "attacking" : "moving";
            state.detail = "point_target_received";
            PublishTaskState(state);
        }
        return;
    }

    if (!msg.contains("type")) {
        ws_manager_.send(session, json_stringify(boost::json::object{
            {"type", "error"}, {"code", 400}, {"message", "missing mode or type"}}));
        return;
    }

    std::string type = std::string(msg.at("type").as_string());

    if (type == "move") {
        ws_manager_.send(session, json_stringify(boost::json::object{
            {"type", "error"}, {"code", 400}, {"message", "move command must use mode field"}}));
        return;
    }

    if (type == "pause") {
        bool any_ok = false;
        std::vector<int> affected_ids;
        if (msg.contains("drone_ids") && msg.at("drone_ids").is_array()) {
            for (const auto& id_value : msg.at("drone_ids").as_array()) {
                const int drone_id = drone_id_from_string(value_to_string(id_value));
                const bool ok = drone_mgr_.ProcessPauseCommand(drone_id);
                if (ok) affected_ids.push_back(drone_id);
                any_ok = ok || any_ok;
            }
        } else {
            std::string drone_id_str = msg.contains("drone_id") ? value_to_string(msg.at("drone_id")) : "";
            const int drone_id = drone_id_from_string(drone_id_str);
            any_ok = drone_mgr_.ProcessPauseCommand(drone_id);
            if (any_ok) affected_ids.push_back(drone_id);
        }
        for (int drone_id : affected_ids) {
            DroneTaskState state;
            {
                std::lock_guard<std::mutex> lock(task_state_mutex_);
                auto it = task_states_.find(drone_id);
                if (it != task_states_.end()) state = it->second;
                paused_previous_states_[drone_id] = state.state.empty() ? "standby" : state.state;
            }
            state.drone_id = drone_id;
            state.state = "paused";
            state.detail = "operator_pause";
            PublishTaskState(state);
        }
        if (!any_ok) {
            ws_manager_.send(session, json_stringify(boost::json::object{
                {"type", "error"}, {"code", 404}, {"message", "no matching drone paused"}}));
        } else if (msg.contains("request_id")) {
            ws_manager_.send(session, json_stringify(boost::json::object{
                {"type", "command_ack"},
                {"command", "pause"},
                {"request_id", value_to_string(msg.at("request_id"))},
            }));
        }
        return;
    }

    if (type == "resume") {
        bool any_ok = false;
        std::vector<int> affected_ids;
        if (msg.contains("drone_ids") && msg.at("drone_ids").is_array()) {
            for (const auto& id_value : msg.at("drone_ids").as_array()) {
                const int drone_id = drone_id_from_string(value_to_string(id_value));
                const bool ok = drone_mgr_.ProcessResumeCommand(drone_id);
                if (ok) affected_ids.push_back(drone_id);
                any_ok = ok || any_ok;
            }
        } else {
            std::string drone_id_str = msg.contains("drone_id") ? value_to_string(msg.at("drone_id")) : "";
            const int drone_id = drone_id_from_string(drone_id_str);
            any_ok = drone_mgr_.ProcessResumeCommand(drone_id);
            if (any_ok) affected_ids.push_back(drone_id);
        }
        for (int drone_id : affected_ids) {
            DroneTaskState state;
            {
                std::lock_guard<std::mutex> lock(task_state_mutex_);
                auto it = task_states_.find(drone_id);
                if (it != task_states_.end()) state = it->second;
                auto previous = paused_previous_states_.find(drone_id);
                state.state = previous != paused_previous_states_.end() ? previous->second : "standby";
                if (previous != paused_previous_states_.end()) paused_previous_states_.erase(previous);
            }
            state.drone_id = drone_id;
            state.detail = "operator_resume";
            PublishTaskState(state);
        }
        if (!any_ok) {
            ws_manager_.send(session, json_stringify(boost::json::object{
                {"type", "error"}, {"code", 404}, {"message", "no matching drone resumed"}}));
        } else if (msg.contains("request_id")) {
            ws_manager_.send(session, json_stringify(boost::json::object{
                {"type", "command_ack"},
                {"command", "resume"},
                {"request_id", value_to_string(msg.at("request_id"))},
            }));
        }
        return;
    }

    ws_manager_.send(session, json_stringify(boost::json::object{
        {"type", "error"}, {"code", 400}, {"message", "unknown type: " + type}}));
}

// ============================================================
// WebSocket 会话
// ============================================================
void HttpServer::RunWsSession(tcp::socket socket,
                               http::request<http::string_body> upgrade_req)
{
    ws_ns::stream<tcp::socket> ws(std::move(socket));
    try {
        ws.set_option(ws_ns::stream_base::timeout::suggested(beast::role_type::server));
        ws.set_option(ws_ns::stream_base::decorator([](ws_ns::response_type& res) {
            res.set(http::field::server, "UE5DroneControl-Backend/1.0");
        }));
        ws.accept(upgrade_req);
    } catch (const std::exception& e) {
        spdlog::warn("[WS] accept failed: {}", e.what());
        return;
    }

    auto session = ws_manager_.add(&ws);
    spdlog::debug("[WS] Client connected, total sessions={}", ws_manager_.count());
    beast::flat_buffer buf;

    while (true) {
        try {
            buf.clear();
            ws.read(buf);
            std::string payload = beast::buffers_to_string(buf.data());

            boost::json::value jv;
            try {
                jv = boost::json::parse(payload);
            } catch (...) {
                ws_manager_.send(session, json_stringify(boost::json::object{
                    {"type", "error"}, {"code", 400}, {"message", "invalid JSON"}}));
                continue;
            }

            if (!jv.is_object()) {
                ws_manager_.send(session, json_stringify(boost::json::object{
                    {"type", "error"}, {"code", 400}, {"message", "payload must be object"}}));
                continue;
            }

            try {
                spdlog::debug("[WS Recv] {}", payload);
                HandleWsCommand(jv.as_object(), session);
            } catch (const ApiError& e) {
                ws_manager_.send(session, json_stringify(boost::json::object{
                    {"type", "error"}, {"code", e.status_code}, {"message", e.what()}}));
            }
        } catch (const std::exception& e) {
            spdlog::info("[WS] session closed: {}", e.what());
            break;
        }
    }

    session->alive = false;
    ws_manager_.remove(&ws);
    spdlog::debug("[WS] Client disconnected, total sessions={}", ws_manager_.count());
}

// ============================================================
// 连接处理（静态）
// ============================================================
void HttpServer::HandleHttpConnection(tcp::socket socket, HttpServer* self) {
    beast::flat_buffer buf;
    http::request<http::string_body> req;
    try {
        http::read(socket, buf, req);
        auto res = self->HandleHttp(req);
        http::write(socket, res);
    } catch (const std::exception& e) {
        spdlog::warn("[HTTP] connection error: {}", e.what());
    }
}

void HttpServer::HandleWsConnection(tcp::socket socket, HttpServer* self) {
    beast::flat_buffer buf;
    http::request<http::string_body> req;
    try {
        http::read(socket, buf, req);
        if (!ws_ns::is_upgrade(req)) {
            auto res = MakeResponse(req, 400, json_error("websocket upgrade required"));
            http::write(socket, res);
            return;
        }
        self->RunWsSession(std::move(socket), std::move(req));
    } catch (const std::exception& e) {
        spdlog::warn("[WS] connection error: {}", e.what());
    }
}

// ============================================================
// 服务器循环
// ============================================================
void HttpServer::RunHttpServer() {
    net::io_context ctx{1};
    tcp::acceptor acceptor(ctx, tcp::endpoint{net::ip::make_address("0.0.0.0"),
                                               static_cast<unsigned short>(config_.http_port)});
    acceptor.set_option(net::socket_base::reuse_address(true));
    spdlog::info("[HTTP] Listening on 0.0.0.0:{}", config_.http_port);

    while (running_) {
        tcp::socket socket(ctx);
        acceptor.accept(socket);
        {
            std::lock_guard<std::mutex> lock(conn_threads_mutex_);
            conn_threads_.emplace_back(HandleHttpConnection, std::move(socket), this);
        }
    }
}

void HttpServer::RunWsServer() {
    net::io_context ctx{1};
    tcp::acceptor acceptor(ctx, tcp::endpoint{net::ip::make_address("0.0.0.0"),
                                               static_cast<unsigned short>(config_.ws_port)});
    acceptor.set_option(net::socket_base::reuse_address(true));
    spdlog::info("[WS] Listening on 0.0.0.0:{}", config_.ws_port);

    while (running_) {
        tcp::socket socket(ctx);
        acceptor.accept(socket);
        {
            std::lock_guard<std::mutex> lock(conn_threads_mutex_);
            conn_threads_.emplace_back(HandleWsConnection, std::move(socket), this);
        }
    }
}

// ============================================================
// 持久化
// ============================================================
void HttpServer::SaveDrones() {
    try {
        std::filesystem::path p(config_.storage_path);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& rec : drone_records_) {
            arr.push_back({
                {"id",        rec.id},
                {"name",      rec.name},
                {"model",     rec.model},
                {"slot",      rec.slot},
                {"ip",        rec.ip},
                {"port",      rec.port},
                {"video_url", rec.video_url},
            });
        }
        std::string data = arr.dump(2);
        std::ofstream f(config_.storage_path, std::ios::trunc);
        f.exceptions(std::ios::failbit | std::ios::badbit);
        f << data;
        f.close();
        if (!f) {
            throw std::runtime_error("write verification failed");
        }
    } catch (const std::exception& e) {
        spdlog::error("[Storage] save failed: {}", e.what());
    }
}

void HttpServer::LoadDrones() {
    try {
        std::ifstream f(config_.storage_path);
        if (!f.is_open()) return;

        nlohmann::json arr;
        f >> arr;
        if (!arr.is_array()) return;

        std::lock_guard<std::mutex> lock(records_mutex_);
        for (const auto& item : arr) {
            DroneRecord rec;
            rec.id        = item.value("id",        "");
            rec.name      = item.value("name",      "");
            rec.model     = item.value("model",     "");
            rec.slot      = item.value("slot",      0);
            rec.ip        = item.value("ip",        "");
            rec.port      = item.value("port",      0);
            rec.video_url = item.value("video_url", "");

            if (rec.id.empty()) continue;

            int drone_id = drone_id_from_string(rec.id);
            auto mapping = config_.port_map.find(rec.slot);
            int send_port = mapping != config_.port_map.end() ? mapping->second.send_port : rec.port;
            if (rec.port == 0) rec.port = send_port;
            drone_mgr_.AddDrone(drone_id, rec.slot, rec.name, config_.jetson_host, send_port);
            drone_records_.push_back(rec);

            // 更新序号
            if (drone_id >= next_drone_seq_) next_drone_seq_ = drone_id + 1;
        }
        spdlog::info("[Storage] Loaded {} drones from {}", drone_records_.size(), config_.storage_path);
    } catch (const std::exception& e) {
        spdlog::warn("[Storage] load failed: {}", e.what());
    }
}
