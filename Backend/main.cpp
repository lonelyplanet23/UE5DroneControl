#include "core/logger.h"
#include "core/config_loader.h"
#include "core/types.h"
#include "communication/udp_receiver.h"
#include "communication/udp_sender.h"
#include "communication/ws_manager.h"
#include "drone/heartbeat_manager.h"
#include "drone/drone_manager.h"
#include "execution/assembly_controller.h"
#include "http/http_server.h"
#include <spdlog/spdlog.h>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>

static std::atomic<bool> g_running{true};

void signal_handler(int)
{
    spdlog::info("Shutdown signal received...");
    g_running = false;
}

int main(int argc, char* argv[])
{
    // 1. 加载配置
    auto config = LoadConfig(argc > 1 ? argv[1] : "config.yaml");

    // 2. 验证配置
    ValidateConfig(config);

    // 3. 初始化日志
    InitLogger(config.log_level, config.log_file);

    spdlog::info("============================================");
    spdlog::info("  Drone Backend v1.0.0");
    spdlog::info("  HTTP :{}  WS :{}", config.http_port, config.ws_port);
    spdlog::info("============================================");

    // 4. 信号处理（在线程启动前注册）
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 5. 初始化 Asio io_context
    boost::asio::io_context io_context;

    // 6. 创建各模块
    UdpSender          udp_sender(io_context);
    HeartbeatManager   hb_manager(udp_sender);
    DroneManager       drone_mgr(hb_manager);
    AssemblyController assembly_ctrl(config.assembly_timeout_sec);
    UdpReceiver        udp_receiver(io_context);
    WsManager          ws_manager;
    HttpServer         http_server(config, drone_mgr, assembly_ctrl, ws_manager);

    // 7. 配置 UDP 遥测接收
    for (const auto& [slot, mapping] : config.port_map) {
        udp_receiver.AddPort(slot, mapping.recv_port, slot);
    }

    // 8. 配置 UdpSender 目标（每个 slot → Jetson IP + send_port）
    for (const auto& [slot, mapping] : config.port_map) {
        udp_sender.SetTarget(slot, config.jetson_host, mapping.send_port);
    }

    // 9. 遥测回调 → DroneManager
    udp_receiver.SetCallback([&](int slot, const TelemetryData& tel) {
        drone_mgr.OnTelemetryReceivedBySlot(slot, tel);

        if (assembly_ctrl.GetState() == AssemblyState::Assembling) {
            int drone_id = drone_mgr.ResolveDroneIdBySlot(slot);
            assembly_ctrl.UpdateDronePosition(
                drone_id > 0 ? drone_id : slot,
                tel.position_ned[0],
                tel.position_ned[1],
                tel.position_ned[2]);
        }
    });

    // 10. 启动 UDP 接收
    udp_receiver.Start();

    // 11. 启动 Asio worker 线程
    auto asio_worker = std::thread([&]() {
        while (g_running) {
            try {
                io_context.run_for(std::chrono::milliseconds(100));
            } catch (...) {}
        }
    });

    // 12. 启动 HTTP/WS 服务器（后台线程）
    auto server_thread = std::thread([&]() {
        http_server.Run();
    });

    // 13. 主循环（超时检查）
    spdlog::info("Backend started. Press Ctrl+C to stop.");

    while (g_running) {
        drone_mgr.CheckTimeouts(config.lost_timeout_sec);

        if (assembly_ctrl.CheckTimeout()) {
            auto p = assembly_ctrl.GetProgress();
            spdlog::warn("[Main] Assembly timeout! {}/{}", p.ready_count, p.total_count);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    spdlog::info("Shutting down...");

    // 清理
    http_server.Stop();
    udp_receiver.Stop();
    hb_manager.StopAll();

    if (asio_worker.joinable())  asio_worker.join();
    if (server_thread.joinable()) server_thread.join();

    spdlog::info("Backend shutdown complete.");
    return 0;
}
