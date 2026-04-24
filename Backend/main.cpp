#include "core/logger.h"
#include "core/config_loader.h"
#include "core/types.h"
#include "communication/udp_receiver.h"
#include "communication/udp_sender.h"
#include "drone/heartbeat_manager.h"
#include "drone/drone_manager.h"
#include "execution/assembly_controller.h"
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
    auto config = LoadConfig("config.yaml");

    // 2. 验证配置
    ValidateConfig(config);

    // 3. 初始化日志
    InitLogger(config.log_level, config.log_file);

    spdlog::info("============================================");
    spdlog::info("  Drone Backend v1.0.0");
    spdlog::info("  HTTP :{}  WS :{}", config.http_port, config.ws_port);
    spdlog::info("============================================");

    // 3. 初始化 Asio io_context
    boost::asio::io_context io_context;

    // 4. 创建各模块
    UdpSender          udp_sender(io_context);
    HeartbeatManager   hb_manager(udp_sender);
    DroneManager       drone_mgr(hb_manager);
    AssemblyController assembly_ctrl(config.assembly_timeout_sec);
    UdpReceiver        udp_receiver(io_context);

    // 5. 配置 UDP 遥测接收
    for (const auto& [slot, mapping] : config.port_map) {
        udp_receiver.AddPort(slot, mapping.recv_port, slot);
    }

    // 6. 信号处理（在线程启动前注册）
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 7. 遥测回调 → DroneManager
    udp_receiver.SetCallback([&](int drone_id, const TelemetryData& tel) {
        drone_mgr.OnTelemetryReceived(drone_id, tel);

        // 同时更新集结进度（如果在集结中）
        if (assembly_ctrl.GetState() == AssemblyState::Assembling) {
            assembly_ctrl.UpdateDronePosition(
                drone_id,
                tel.position_ned[0],
                tel.position_ned[1],
                tel.position_ned[2]);
        }
    });

    // 7. 注册示例无人机（实际由 HTTP 注册接口调用）
    // 默认添加 3 架，端口按 slot 配置
    for (int i = 1; i <= 3; ++i) {
        if (config.port_map.find(i) == config.port_map.end()) continue;
        drone_mgr.AddDrone(i, i, "UAV" + std::to_string(i));
    }

    // 8. 集结进度回调
    assembly_ctrl.SetProgressCallback([&](const AssemblyProgress& progress) {
        spdlog::info("[AssemblyCB] {}: {}/{} ready",
                     progress.array_id, progress.ready_count, progress.total_count);
        // TODO: 实际项目中通过 WS 推送
    });

    // 9. 启动 UDP 接收
    udp_receiver.Start();

    // 10. 启动 Asio worker 线程（处理 UDP 异步收发）
    auto asio_worker = std::thread([&]() {
        while (g_running) {
            try {
                io_context.run_for(std::chrono::milliseconds(100));
            } catch (...) {}
        }
    });

    // 11. 主循环
    spdlog::info("Backend started. Press Ctrl+C to stop.");

    while (g_running) {
        // 超时检查（每秒）
        drone_mgr.CheckTimeouts(config.lost_timeout_sec);

        // 集结超时检查
        if (assembly_ctrl.CheckTimeout()) {
            auto p = assembly_ctrl.GetProgress();
            spdlog::warn("[Main] Assembly timeout! {}/{}", p.ready_count, p.total_count);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    spdlog::info("Shutting down...");

    // 清理
    udp_receiver.Stop();
    hb_manager.StopAll();

    if (asio_worker.joinable()) asio_worker.join();

    spdlog::info("Backend shutdown complete.");
    return 0;
}
