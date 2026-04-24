#pragma once

#include "core/types.h"
#include "communication/udp_sender.h"
#include <memory>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

/// 心跳线程获取指令的回调
/// 返回 true 表示取到了待发送指令，false 表示队列为空
using CommandProvider = std::function<bool(DroneControlPacket& cmd)>;

/// 每架无人机的心跳状态
struct HeartbeatState {
    std::atomic<bool> running{false};
    std::atomic<double> last_ned_x{0.0};
    std::atomic<double> last_ned_y{0.0};
    std::atomic<double> last_ned_z{-1.0};
    CommandProvider    get_command;   // 从指令队列取指令
};

/// 心跳维持管理器
///
/// 每架 Online 无人机启动一个独立线程，以 ≥2Hz 发送 24 字节控制包
/// - 如果指令队列中有待发送指令 → Pop 并以 Mode=1 发送
/// - 如果队列为空 → 以 Mode=0（悬停）发送最后已知位置
class HeartbeatManager {
public:
    explicit HeartbeatManager(UdpSender& sender);
    ~HeartbeatManager();

    /// 启动无人机心跳
    /// @param drone_id      无人机 ID
    /// @param jetson_ip     目标 IP
    /// @param send_port     目标端口
    /// @param cmd_provider  回调：从指令队列取指令（可为空）
    void Start(int drone_id, const std::string& jetson_ip, int send_port,
               CommandProvider cmd_provider = nullptr);

    /// 更新无人机的最后 NED 位置（由 DroneManager 遥测/控制时调用）
    void UpdateLastPosition(int drone_id, double x, double y, double z);

    /// 停止无人机心跳
    void Stop(int drone_id);

    /// 停止所有心跳
    void StopAll();

private:
    // 无锁版本的 Stop，Start 内部调用（已持有 mutex 时使用）
    void StopInternal(int drone_id);

    void HeartbeatLoop(int drone_id);

    UdpSender& sender_;
    std::unordered_map<int, std::shared_ptr<HeartbeatState>> states_;
    std::unordered_map<int, std::unique_ptr<std::thread>> threads_;
    mutable std::mutex mutex_;
};
