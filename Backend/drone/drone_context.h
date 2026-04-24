#pragma once

#include "core/types.h"
#include "command_queue.h"
#include "state_machine.h"
#include <memory>
#include <string>

/// 单无人机上下文
///
/// 聚合了某架无人机的所有运行时状态：
/// - 身份信息
/// - 连接状态
/// - 指令队列
/// - 最近遥测缓存
/// - 心跳信息
struct DroneContext {
    // === 身份信息 ===
    int     drone_id = 0;
    int     slot = 0;               // 编号 1~6
    std::string name;

    // === 运行时状态 ===
    std::unique_ptr<StateMachine>   state_machine;
    std::unique_ptr<CommandQueue>   command_queue;

    TelemetryData   latest_telemetry{};
    bool            has_telemetry = false;

    // === 心跳 ===
    double  last_heartbeat_time = 0.0;
    uint64_t heartbeat_count = 0;

    // === 最后发送的 NED 位置（失联后维持） ===
    double  last_ned_x = 0.0;
    double  last_ned_y = 0.0;
    double  last_ned_z = -1.0;       // 默认高度 -1m（安全高度）

    // === 无人机映射到 Jetson 的目标地址 ===
    std::string jetson_ip = "192.168.30.104";
    int         send_port = 8889;    // UDP 控制包发送端口

    DroneContext(int id, int slot_id, const std::string& drone_name)
        : drone_id(id)
        , slot(slot_id)
        , name(drone_name)
        , state_machine(std::make_unique<StateMachine>())
        , command_queue(std::make_unique<CommandQueue>())
    {
    }
};
