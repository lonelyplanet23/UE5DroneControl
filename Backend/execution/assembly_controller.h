#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <atomic>

/// 集结状态
enum class AssemblyState : uint8_t {
    Idle,
    Assembling,
    Ready,
    Executing,
    Timeout
};

/// 集结任务配置
struct AssemblyConfig {
    std::string array_id;
    std::string mode;  // "recon" / "patrol" / "attack"

    struct Path {
        int path_id;
        std::string drone_id;
        bool closed_loop = false;

        struct Waypoint {
            double x, y, z;    // UE 偏移坐标（厘米）
            float segment_speed = 0;
            float wait_time = 0;
        };
        std::vector<Waypoint> waypoints;
    };
    std::vector<Path> paths;
};

/// 集结控制器
///
/// 职责：
/// 1. 接收 POST /api/arrays → 进入 ASSEMBLING 状态
/// 2. 计算每架无人机的初始槽位目标点（NED）
/// 3. 监控各机到达状态 → 推送进度
/// 4. 超时检测（60s）
///
/// 状态机：
///   Idle → ASSEMBLING → READY → EXECUTING / Timeout → Idle
class AssemblyController {
public:
    using ProgressCallback = std::function<void(const AssemblyProgress&)>;

    explicit AssemblyController(int timeout_sec = 60);

    /// 开始集结
    /// @param config  集结任务配置
    /// @return true 如果成功启动
    bool Start(const AssemblyConfig& config);

    /// 更新无人机位置（由 DroneManager 遥测回调调用）
    /// @param drone_id  无人机 ID
    /// @param ned_x, ned_y, ned_z  NED 位置（米）
    void UpdateDronePosition(int drone_id, double ned_x, double ned_y, double ned_z);

    /// 检查超时
    bool CheckTimeout();

    /// 停止集结
    void Stop();

    /// 获取当前集结状态
    AssemblyState GetState() const { return state_; }

    /// 获取集结进度
    AssemblyProgress GetProgress() const;

    /// 注册进度回调
    void SetProgressCallback(ProgressCallback cb);

private:
    AssemblyState state_ = AssemblyState::Idle;
    int timeout_sec_ = 60;
    AssemblyConfig config_;

    // 每架无人机的到位状态
    struct DroneArrival {
        int drone_id;
        double target_ned_x, target_ned_y, target_ned_z;
        bool arrived = false;
    };
    std::vector<DroneArrival> arrivals_;

    std::chrono::steady_clock::time_point start_time_;
    ProgressCallback progress_cb_;
};
