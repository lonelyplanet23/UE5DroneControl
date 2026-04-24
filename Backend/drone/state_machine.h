#pragma once

#include "core/types.h"
#include <chrono>
#include <functional>
#include <atomic>

/// 无人机连接状态机
///
/// 状态转移:
///   Offline ──(收到首包遥测)──→ Online
///   Online  ──(10s 无遥测)──→ Lost
///   Lost    ──(收到新遥测)────→ Online (重连)
///
/// 状态变更时通过回调通知外部（WebSocket 推送）
class StateMachine {
public:
    using OnStateChange = std::function<void(StateEvent event)>;

    explicit StateMachine(OnStateChange on_change = nullptr);

    /// 获取当前状态
    DroneConnectionState GetState() const;

    /// 收到遥测数据时调用 → 重置超时计时器
    /// @return 返回 StateEvent（PowerOn 或 Reconnect），无事件返回 std::nullopt
    ///         调用方根据返回值触发 GPS 锚点记录和 WS 推送
    void OnTelemetryReceived();

    /// 超时检查（由监控线程每秒调用）
    /// @param timeout_sec  超时阈值（秒）
    /// @return true 如果状态发生了变化
    bool CheckTimeout(int timeout_sec);

    /// 重置状态为 Offline
    void Reset();

private:
    std::atomic<DroneConnectionState> state_{DroneConnectionState::Offline};
    std::chrono::steady_clock::time_point last_telemetry_time_;
    OnStateChange on_state_change_;
};
