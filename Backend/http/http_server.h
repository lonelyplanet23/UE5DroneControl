#pragma once

#define _WIN32_WINNT 0x0601

#include "core/config_loader.h"
#include "core/types.h"
#include "drone/drone_manager.h"
#include "execution/assembly_controller.h"
#include "communication/ws_manager.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace ws_ns = beast::websocket;
namespace net   = boost::asio;
using tcp = net::ip::tcp;

// ============================================================
// ApiError
// ============================================================
struct ApiError : public std::runtime_error {
    int status_code = 500;
    ApiError(int status, const std::string& msg)
        : std::runtime_error(msg), status_code(status) {}
};

// ============================================================
// JSON helpers
// ============================================================
inline std::string json_stringify(const boost::json::value& v) {
    return boost::json::serialize(v);
}

inline const boost::json::object& require_object(const boost::json::value& v, const char* ctx) {
    if (!v.is_object()) throw ApiError(400, std::string(ctx) + " must be a JSON object");
    return v.as_object();
}

inline const boost::json::array& require_array(const boost::json::value& v, const char* ctx) {
    if (!v.is_array()) throw ApiError(400, std::string(ctx) + " must be a JSON array");
    return v.as_array();
}

// ============================================================
// HttpServer
//
// 职责：
//   1. 监听 HTTP REST 端口，处理 /api/drones、/api/arrays 等接口
//   2. 监听 WebSocket 端口，处理 move/pause/resume 指令
//   3. 通过 DroneManager 和 AssemblyController 执行业务逻辑
//   4. 通过 WsManager 向 UE5 推送遥测/事件/告警
// ============================================================
class HttpServer {
public:
    HttpServer(const AppConfig& config,
               DroneManager& drone_mgr,
               AssemblyController& assembly_ctrl,
               WsManager& ws_manager);

    /// 启动 HTTP 和 WebSocket 监听线程（阻塞直到 stop() 被调用）
    void Run();

    /// 停止服务器
    void Stop();

    bool IsRunning() const { return running_; }

private:
    // ---- HTTP 处理 ----
    http::response<http::string_body> HandleHttp(
        http::request<http::string_body>& req);

    // ---- WebSocket 会话 ----
    void RunWsSession(tcp::socket socket,
                      http::request<http::string_body> upgrade_req);

    // ---- 连接处理 ----
    static void HandleHttpConnection(tcp::socket socket, HttpServer* self);
    static void HandleWsConnection(tcp::socket socket, HttpServer* self);

    // ---- 服务器循环 ----
    void RunHttpServer();
    void RunWsServer();

    // ---- REST 路由实现 ----
    boost::json::value ApiListDrones();
    boost::json::value ApiRegisterDrone(const boost::json::object& body);
    boost::json::value ApiUpdateDrone(const std::string& id, const boost::json::object& body);
    boost::json::value ApiDeleteDrone(const std::string& id);
    boost::json::value ApiGetAnchor(const std::string& id);
    boost::json::value ApiCreateArray(const boost::json::object& body);
    boost::json::value ApiStopArray(const std::string& id);

    // ---- Debug 路由 ----
    boost::json::value DebugDroneState(const std::string& id);
    boost::json::value DebugDroneQueue(const std::string& id);
    boost::json::value DebugHeartbeat(const std::string& id);
    boost::json::value DebugInjectTelemetry(const std::string& id, const boost::json::object& body);
    boost::json::value DebugMove(const std::string& id, const boost::json::object& body);
    boost::json::value DebugPause(const std::string& id, bool pause);
    boost::json::value DebugSingleArray(const std::string& id, const boost::json::object& body);
    boost::json::value DebugTarget(const std::string& id, const boost::json::object& body);
    boost::json::value DebugBatchArray(const boost::json::array& body);
    boost::json::value DebugArrayState(const std::string& id);

    // ---- WS 命令处理 ----
    void HandleWsCommand(const boost::json::object& msg,
                         const std::shared_ptr<WsSession>& session);

    // ---- 持久化 ----
    void SaveDrones();
    void LoadDrones();

    // ---- 工具 ----
    static bool PathMatch(const std::string& path,
                          const std::string& prefix,
                          const std::string& suffix,
                          std::string& id_out);

    static http::response<http::string_body> MakeResponse(
        const http::request<http::string_body>& req,
        int status_code,
        const std::string& body,
        const std::string& content_type = "application/json");

    // ---- 成员 ----
    const AppConfig&      config_;
    DroneManager&         drone_mgr_;
    AssemblyController&   assembly_ctrl_;
    WsManager&            ws_manager_;

    std::atomic<bool>     running_{false};
    std::thread           http_thread_;
    std::thread           ws_thread_;

    // 无人机注册表（持久化用）
    struct DroneRecord {
        std::string id;
        std::string name;
        std::string model;
        int         slot = 0;
        std::string ip;
        int         port = 0;
        std::string video_url;
    };
    std::vector<DroneRecord> drone_records_;
    mutable std::mutex       records_mutex_;
    int                      next_drone_seq_ = 1;
};
