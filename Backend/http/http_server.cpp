#include "http/http_server.h"
#include "conversion/coordinate_converter.h"
#include "conversion/quaternion_utils.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <chrono>
#include <fstream>
#include <sstream>

// ============================================================
// 构造 / 析构
// ============================================================
HttpServer::HttpServer(const AppConfig& config,
                       DroneManager& drone_mgr,
                       AssemblyController& assembly_ctrl,
                       WsManager& ws_manager)
    : config_(config)
    , drone_mgr_(drone_mgr)
    , assembly_ctrl_(assembly_ctrl)
    , ws_manager_(ws_manager)
{
    // 注册 DroneManager 回调 → WS 推送
    drone_mgr_.SetTelemetryCallback([this](int drone_id, const TelemetryData& tel) {
        double ux, uy, uz;
        CoordinateConverter::NedToUeOffset(
            tel.position_ned[0], tel.position_ned[1], tel.position_ned[2],
            ux, uy, uz);

        double roll, pitch, yaw;
        QuaternionUtils::QuatToEuler(
            tel.quaternion[0], tel.quaternion[1], tel.quaternion[2], tel.quaternion[3],
            roll, pitch, yaw);

        double speed = std::sqrt(
            tel.velocity[0]*tel.velocity[0] +
            tel.velocity[1]*tel.velocity[1] +
            tel.velocity[2]*tel.velocity[2]);

        auto msg = boost::json::object{
            {"type",     "telemetry"},
            {"drone_id", "d" + std::to_string(drone_id)},
            {"x",        ux},
            {"y",        uy},
            {"z",        uz},
            {"pitch",    pitch},
            {"yaw",      yaw},
            {"roll",     roll},
            {"speed",    speed},
            {"battery",  tel.battery},
        };
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
            {"drone_id", "d" + std::to_string(drone_id)},
            {"event",    type},
        };
        if (anchor.valid) {
            msg["gps_lat"] = anchor.latitude;
            msg["gps_lon"] = anchor.longitude;
            msg["gps_alt"] = anchor.altitude;
        }
        ws_manager_.broadcast(json_stringify(msg));
    });

    drone_mgr_.SetAlertCallback([this](int drone_id, const std::string& alert_type, int value) {
        auto msg = boost::json::object{
            {"type",     "alert"},
            {"drone_id", "d" + std::to_string(drone_id)},
            {"alert",    alert_type},
            {"value",    value},
        };
        ws_manager_.broadcast(json_stringify(msg));
    });

    assembly_ctrl_.SetProgressCallback([this](const AssemblyProgress& progress) {
        auto msg = boost::json::object{
            {"type",        "assembling"},
            {"array_id",    progress.array_id},
            {"ready_count", progress.ready_count},
            {"total_count", progress.total_count},
        };
        ws_manager_.broadcast(json_stringify(msg));

        if (progress.ready_count >= progress.total_count && progress.total_count > 0) {
            auto done_msg = boost::json::object{
                {"type",     "assembly_complete"},
                {"array_id", progress.array_id},
            };
            ws_manager_.broadcast(json_stringify(done_msg));
        }
    });

    LoadDrones();
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

static double json_number_to_double(const boost::json::value& value, const char* field_name) {
    if (value.is_double()) return value.as_double();
    if (value.is_int64())  return static_cast<double>(value.as_int64());
    if (value.is_uint64()) return static_cast<double>(value.as_uint64());
    throw ApiError(400, std::string("field must be numeric: ") + field_name);
}

static int json_number_to_int(const boost::json::value& value, const char* field_name) {
    if (value.is_int64())  return static_cast<int>(value.as_int64());
    if (value.is_uint64()) return static_cast<int>(value.as_uint64());
    if (value.is_double()) return static_cast<int>(value.as_double());
    throw ApiError(400, std::string("field must be numeric: ") + field_name);
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
        if (method == "POST" && PathMatch(path, "/api/debug/cmd/", "/array", id))
            return MakeResponse(req, 200, json_stringify(DebugSingleArray(id, require_object(body, "body"))));
        if (method == "POST" && PathMatch(path, "/api/debug/cmd/", "/target", id))
            return MakeResponse(req, 200, json_stringify(DebugTarget(id, require_object(body, "body"))));
        if (method == "POST" && path == "/api/debug/cmd/batch/array")
            return MakeResponse(req, 200, json_stringify(DebugBatchArray(require_array(body, "body"))));
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
    // "d1" → 1, "1" → 1
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
        int drone_id = drone_id_from_string(rec.id);
        auto status = drone_mgr_.GetStatus(drone_id);

        int ue_recv_port = 0;
        std::string topic_prefix;
        auto pm_it = config_.port_map.find(rec.slot);
        if (pm_it != config_.port_map.end()) {
            ue_recv_port  = pm_it->second.recv_port;
            topic_prefix  = pm_it->second.ros_topic_prefix;
        }

        arr.push_back(boost::json::object{
            {"id",                rec.id},
            {"name",              rec.name},
            {"model",             rec.model},
            {"slot",              rec.slot},
            {"ip",                rec.ip},
            {"port",              rec.port},
            {"video_url",         rec.video_url},
            {"status",            status.connection_state},
            {"battery",           status.battery},
            {"mavlink_system_id", rec.slot},
            {"bit_index",         rec.slot - 1},
            {"ue_receive_port",   ue_recv_port},
            {"topic_prefix",      topic_prefix},
        });
    }
    return boost::json::object{{"drones", arr}};
}

boost::json::value HttpServer::ApiRegisterDrone(const boost::json::object& body) {
    std::lock_guard<std::mutex> lock(records_mutex_);

    if (static_cast<int>(drone_records_.size()) >= config_.max_count)
        throw ApiError(507, "max drone count reached");

    DroneRecord rec;
    rec.id        = "d" + std::to_string(next_drone_seq_++);
    rec.name      = body.contains("name")      ? std::string(body.at("name").as_string())      : rec.id;
    rec.model     = body.contains("model")     ? std::string(body.at("model").as_string())     : "";
    rec.slot      = body.contains("slot")      ? static_cast<int>(body.at("slot").as_int64())  : next_drone_seq_ - 1;
    rec.ip        = body.contains("ip")        ? std::string(body.at("ip").as_string())        : "";
    rec.port      = body.contains("port")      ? static_cast<int>(body.at("port").as_int64())  : 0;
    rec.video_url = body.contains("video_url") ? std::string(body.at("video_url").as_string()) : "";

    // duplicate name / slot check
    for (const auto& existing : drone_records_) {
        if (existing.name == rec.name)
            throw ApiError(409, "name already in use: " + rec.name);
        if (existing.slot == rec.slot)
            throw ApiError(409, "slot already in use: " + std::to_string(rec.slot));
    }

    int drone_id = drone_id_from_string(rec.id);

    // 配置 Jetson 目标地址
    auto it = config_.port_map.find(rec.slot);
    if (it == config_.port_map.end())
        throw ApiError(400, "slot " + std::to_string(rec.slot) + " not in port_map");

    drone_mgr_.AddDrone(
        drone_id,
        rec.slot,
        rec.name,
        config_.jetson_host,
        it->second.send_port);
    drone_records_.push_back(rec);
    SaveDrones();

    spdlog::info("[HTTP] Registered drone {} slot={}", rec.id, rec.slot);
    return boost::json::object{{"id", rec.id}, {"slot", rec.slot}, {"name", rec.name}};
}

boost::json::value HttpServer::ApiUpdateDrone(const std::string& id, const boost::json::object& body) {
    std::lock_guard<std::mutex> lock(records_mutex_);
    for (auto& rec : drone_records_) {
        if (rec.id != id) continue;
        if (body.contains("name"))      rec.name      = std::string(body.at("name").as_string());
        if (body.contains("model"))     rec.model     = std::string(body.at("model").as_string());
        if (body.contains("video_url")) rec.video_url = std::string(body.at("video_url").as_string());
        SaveDrones();
        return boost::json::object{{"id", rec.id}, {"updated", true}};
    }
    throw ApiError(404, "drone not found: " + id);
}

boost::json::value HttpServer::ApiDeleteDrone(const std::string& id) {
    std::lock_guard<std::mutex> lock(records_mutex_);
    for (auto it = drone_records_.begin(); it != drone_records_.end(); ++it) {
        if (it->id != id) continue;
        drone_mgr_.RemoveDrone(drone_id_from_string(id));
        drone_records_.erase(it);
        SaveDrones();
        return boost::json::object{{"id", id}, {"deleted", true}};
    }
    throw ApiError(404, "drone not found: " + id);
}

boost::json::value HttpServer::ApiGetAnchor(const std::string& id) {
    int drone_id = drone_id_from_string(id);
    if (!drone_mgr_.HasDrone(drone_id))
        throw ApiError(404, "drone not found: " + id);
    auto anchor = drone_mgr_.GetAnchor(drone_id);
    if (!anchor.valid)
        throw ApiError(425, "drone not yet powered on");
    return boost::json::object{
        {"drone_id",  id},
        {"gps_lat",   anchor.latitude},
        {"gps_lon",   anchor.longitude},
        {"gps_alt",   anchor.altitude},
    };
}

boost::json::value HttpServer::ApiCreateArray(const boost::json::object& body) {
    AssemblyConfig cfg;
    cfg.array_id = body.contains("array_id") ? std::string(body.at("array_id").as_string()) : "a1";
    cfg.mode     = body.contains("mode")     ? std::string(body.at("mode").as_string())     : "recon";

    if (body.contains("paths") && body.at("paths").is_array()) {
        for (const auto& p : body.at("paths").as_array()) {
            if (!p.is_object()) continue;
            const auto& po = p.as_object();
            AssemblyConfig::Path path;
            // support both camelCase (API doc) and snake_case
            if (po.contains("pathId"))   path.path_id = static_cast<int>(po.at("pathId").as_int64());
            else if (po.contains("path_id")) path.path_id = static_cast<int>(po.at("path_id").as_int64());
            path.drone_id = po.contains("drone_id") ? std::string(po.at("drone_id").as_string()) : "";
            if (po.contains("bClosedLoop"))  path.closed_loop = po.at("bClosedLoop").as_bool();
            else if (po.contains("closed_loop")) path.closed_loop = po.at("closed_loop").as_bool();

            if (po.contains("waypoints") && po.at("waypoints").is_array()) {
                for (const auto& w : po.at("waypoints").as_array()) {
                    if (!w.is_object()) continue;
                    const auto& wo = w.as_object();
                    AssemblyConfig::Path::Waypoint wp;
                    // location.{x,y,z} (API doc) or flat x/y/z (legacy)
                    if (wo.contains("location") && wo.at("location").is_object()) {
                        const auto& loc = wo.at("location").as_object();
                        wp.x = loc.contains("x") ? json_number_to_double(loc.at("x"), "location.x") : 0.0;
                        wp.y = loc.contains("y") ? json_number_to_double(loc.at("y"), "location.y") : 0.0;
                        wp.z = loc.contains("z") ? json_number_to_double(loc.at("z"), "location.z") : 0.0;
                    } else {
                        wp.x = wo.contains("x") ? json_number_to_double(wo.at("x"), "x") : 0.0;
                        wp.y = wo.contains("y") ? json_number_to_double(wo.at("y"), "y") : 0.0;
                        wp.z = wo.contains("z") ? json_number_to_double(wo.at("z"), "z") : 0.0;
                    }
                    if (wo.contains("segmentSpeed"))      wp.segment_speed = static_cast<float>(json_number_to_double(wo.at("segmentSpeed"), "segmentSpeed"));
                    else if (wo.contains("segment_speed")) wp.segment_speed = static_cast<float>(json_number_to_double(wo.at("segment_speed"), "segment_speed"));
                    if (wo.contains("waitTime"))      wp.wait_time = static_cast<float>(json_number_to_double(wo.at("waitTime"), "waitTime"));
                    else if (wo.contains("wait_time")) wp.wait_time = static_cast<float>(json_number_to_double(wo.at("wait_time"), "wait_time"));
                    path.waypoints.push_back(wp);
                }
            }
            cfg.paths.push_back(path);
        }
    }

    if (!assembly_ctrl_.Start(cfg))
        throw ApiError(409, "assembly already in progress");

    return boost::json::object{{"array_id", cfg.array_id}, {"status", "assembling"}};
}

boost::json::value HttpServer::ApiStopArray(const std::string& id) {
    assembly_ctrl_.Stop();
    return boost::json::object{{"array_id", id}, {"status", "stopped"}};
}

// ============================================================
// Debug 路由
// ============================================================
boost::json::value HttpServer::DebugDroneState(const std::string& id) {
    int drone_id = drone_id_from_string(id);
    auto status = drone_mgr_.GetStatus(drone_id);
    return boost::json::object{
        {"id",     id},
        {"status", status.connection_state},
        {"battery", status.battery},
        {"x", status.pos_x}, {"y", status.pos_y}, {"z", status.pos_z},
        {"yaw", status.yaw}, {"speed", status.speed},
    };
}

boost::json::value HttpServer::DebugDroneQueue(const std::string& id) {
    int drone_id = drone_id_from_string(id);
    size_t queue_size = 0;
    bool paused = false;
    DroneControlPacket next_cmd{};
    if (!drone_mgr_.GetQueueDebugInfo(drone_id, queue_size, paused, &next_cmd))
        throw ApiError(404, "drone not found: " + id);

    boost::json::object result{
        {"id", id},
        {"queue_size", static_cast<std::uint64_t>(queue_size)},
        {"paused", paused},
    };

    if (queue_size > 0) {
        result["next_command"] = boost::json::object{
            {"timestamp", next_cmd.timestamp},
            {"x", next_cmd.x},
            {"y", next_cmd.y},
            {"z", next_cmd.z},
            {"mode", next_cmd.mode},
        };
    }

    return result;
}

boost::json::value HttpServer::DebugHeartbeat(const std::string& id) {
    return boost::json::object{{"id", id}, {"heartbeat", "ok"}};
}

boost::json::value HttpServer::DebugInjectTelemetry(const std::string& id, const boost::json::object& body) {
    int drone_id = drone_id_from_string(id);
    if (!drone_mgr_.HasDrone(drone_id))
        throw ApiError(404, "drone not found: " + id);
    TelemetryData tel{};
    tel.battery = -1;
    tel.quaternion[0] = 1.0;

    auto read_vec3 = [](const boost::json::object& obj,
                        const char* key,
                        double out[3]) {
        if (!obj.contains(key) || !obj.at(key).is_array()) return;
        const auto& arr = obj.at(key).as_array();
        if (arr.size() >= 3) {
            out[0] = json_number_to_double(arr[0], key);
            out[1] = json_number_to_double(arr[1], key);
            out[2] = json_number_to_double(arr[2], key);
        }
    };

    if (body.contains("position") && body.at("position").is_array()) {
        const auto& pos = body.at("position").as_array();
        if (pos.size() >= 3) {
            tel.position_ned[0] = json_number_to_double(pos[0], "position[0]");
            tel.position_ned[1] = json_number_to_double(pos[1], "position[1]");
            tel.position_ned[2] = json_number_to_double(pos[2], "position[2]");
        }
    }
    if (body.contains("q") && body.at("q").is_array()) {
        const auto& q = body.at("q").as_array();
        if (q.size() >= 4) {
            tel.quaternion[0] = json_number_to_double(q[0], "q[0]");
            tel.quaternion[1] = json_number_to_double(q[1], "q[1]");
            tel.quaternion[2] = json_number_to_double(q[2], "q[2]");
            tel.quaternion[3] = json_number_to_double(q[3], "q[3]");
        }
    }
    read_vec3(body, "velocity", tel.velocity);
    read_vec3(body, "angular_velocity", tel.angular_velocity);
    read_vec3(body, "local_position", tel.local_position);

    tel.battery = body.contains("battery") ? json_number_to_int(body.at("battery"), "battery") : -1;
    if (body.contains("timestamp")) {
        if (body.at("timestamp").is_int64()) {
            tel.timestamp = static_cast<uint64_t>(body.at("timestamp").as_int64());
        } else if (body.at("timestamp").is_uint64()) {
            tel.timestamp = body.at("timestamp").as_uint64();
        }
    }
    if (body.contains("gps_lat")) tel.gps_lat = json_number_to_double(body.at("gps_lat"), "gps_lat");
    if (body.contains("gps_lon")) tel.gps_lon = json_number_to_double(body.at("gps_lon"), "gps_lon");
    if (body.contains("gps_alt")) tel.gps_alt = json_number_to_double(body.at("gps_alt"), "gps_alt");
    tel.gps_fix = body.contains("gps_fix")
        ? body.at("gps_fix").as_bool()
        : (body.contains("gps_lat") && body.contains("gps_lon") && body.contains("gps_alt"));
    if (body.contains("arming_state")) {
        tel.arming_state = static_cast<uint8_t>(json_number_to_int(body.at("arming_state"), "arming_state"));
    }
    if (body.contains("nav_state")) {
        tel.nav_state = static_cast<uint8_t>(json_number_to_int(body.at("nav_state"), "nav_state"));
    }

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
    double x = body.contains("x") ? json_number_to_double(body.at("x"), "x") : 0.0;
    double y = body.contains("y") ? json_number_to_double(body.at("y"), "y") : 0.0;
    double z = body.contains("z") ? json_number_to_double(body.at("z"), "z") : 0.0;
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
    return DebugMove(id, body);
}

boost::json::value HttpServer::DebugBatchArray(const boost::json::array& body) {
    return boost::json::object{{"status", "stub"}};
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
static int ws_drone_id(const boost::json::object& msg) {
    if (!msg.contains("drone_id")) return 0;
    const auto& v = msg.at("drone_id");
    if (v.is_int64())  return static_cast<int>(v.as_int64());
    if (v.is_uint64()) return static_cast<int>(v.as_uint64());
    if (v.is_string()) return drone_id_from_string(std::string(v.as_string()));
    return 0;
}

void HttpServer::HandleWsCommand(const boost::json::object& msg,
                                  const std::shared_ptr<WsSession>& session)
{
    if (!msg.contains("type")) {
        ws_manager_.send(session, json_stringify(boost::json::object{
            {"type", "error"}, {"code", 400}, {"message", "missing type"}}));
        return;
    }

    std::string type = std::string(msg.at("type").as_string());

    if (type == "move") {
        int drone_id = ws_drone_id(msg);
        double x = msg.contains("x") ? json_number_to_double(msg.at("x"), "x") : 0.0;
        double y = msg.contains("y") ? json_number_to_double(msg.at("y"), "y") : 0.0;
        double z = msg.contains("z") ? json_number_to_double(msg.at("z"), "z") : 0.0;
        if (!drone_mgr_.ProcessMoveCommand(drone_id, x, y, z)) {
            ws_manager_.send(session, json_stringify(boost::json::object{
                {"type", "error"}, {"code", 404}, {"message", "drone not found"}}));
        }
        return;
    }

    if (type == "pause") {
        if (msg.contains("drone_ids") && msg.at("drone_ids").is_array()) {
            for (const auto& v : msg.at("drone_ids").as_array()) {
                int did = v.is_string() ? drone_id_from_string(std::string(v.as_string()))
                                        : static_cast<int>(v.as_int64());
                drone_mgr_.ProcessPauseCommand(did);
            }
        } else {
            drone_mgr_.ProcessPauseCommand(ws_drone_id(msg));
        }
        return;
    }

    if (type == "resume") {
        if (msg.contains("drone_ids") && msg.at("drone_ids").is_array()) {
            for (const auto& v : msg.at("drone_ids").as_array()) {
                int did = v.is_string() ? drone_id_from_string(std::string(v.as_string()))
                                        : static_cast<int>(v.as_int64());
                drone_mgr_.ProcessResumeCommand(did);
            }
        } else {
            drone_mgr_.ProcessResumeCommand(ws_drone_id(msg));
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
    acceptor.non_blocking(true);
    spdlog::info("[HTTP] Listening on 0.0.0.0:{}", config_.http_port);

    while (running_) {
        tcp::socket socket(ctx);
        boost::system::error_code ec;
        acceptor.accept(socket, ec);
        if (ec == net::error::would_block || ec == net::error::try_again) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (ec) {
            if (running_) {
                spdlog::warn("[HTTP] accept error: {}", ec.message());
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }
        std::thread(HandleHttpConnection, std::move(socket), this).detach();
    }
}

void HttpServer::RunWsServer() {
    net::io_context ctx{1};
    tcp::acceptor acceptor(ctx, tcp::endpoint{net::ip::make_address("0.0.0.0"),
                                               static_cast<unsigned short>(config_.ws_port)});
    acceptor.set_option(net::socket_base::reuse_address(true));
    acceptor.non_blocking(true);
    spdlog::info("[WS] Listening on 0.0.0.0:{}", config_.ws_port);

    while (running_) {
        tcp::socket socket(ctx);
        boost::system::error_code ec;
        acceptor.accept(socket, ec);
        if (ec == net::error::would_block || ec == net::error::try_again) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (ec) {
            if (running_) {
                spdlog::warn("[WS] accept error: {}", ec.message());
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }
        std::thread(HandleWsConnection, std::move(socket), this).detach();
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
        std::ofstream f(config_.storage_path);
        f << arr.dump(2);
    } catch (const std::exception& e) {
        spdlog::warn("[Storage] save failed: {}", e.what());
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
            int send_port = 0;
            auto mapping_it = config_.port_map.find(rec.slot);
            if (mapping_it != config_.port_map.end()) {
                send_port = mapping_it->second.send_port;
            }

            drone_mgr_.AddDrone(
                drone_id,
                rec.slot,
                rec.name,
                config_.jetson_host,
                send_port);
            drone_records_.push_back(rec);

            // 更新序号
            if (drone_id >= next_drone_seq_) next_drone_seq_ = drone_id + 1;
        }
        spdlog::info("[Storage] Loaded {} drones from {}", drone_records_.size(), config_.storage_path);
    } catch (const std::exception& e) {
        spdlog::warn("[Storage] load failed: {}", e.what());
    }
}
