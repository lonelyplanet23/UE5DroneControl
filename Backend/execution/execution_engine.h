#pragma once

#include "core/types.h"
#include "execution/assembly_controller.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

/// 单机执行任务
struct DroneTask {
    int drone_id = 0;
    std::string mode;       // "scout" / "patrol" / "attack"
    bool closed_loop = false;
    std::vector<AssemblyConfig::Path::Waypoint> waypoints;
    int current_wp = 0;
    bool target_override = false;
    AssemblyConfig::Path::Waypoint override_target;
};

struct AvoidanceEvent {
    int drone_id = 0;
    int other_drone_id = 0;
    double current_distance_m = 0.0;
    double predicted_distance_m = 0.0;
    double threshold_m = 0.0;
    double base_ned_n = 0.0;
    double base_ned_e = 0.0;
    double base_ned_d = 0.0;
    double applied_ned_n = 0.0;
    double applied_ned_e = 0.0;
    double applied_ned_d = 0.0;
    double offset_n = 0.0;
    double offset_e = 0.0;
    double offset_d = 0.0;
    bool activated = false;
    bool restored = false;
    bool valid = false;
};

struct AvoidanceStats {
    uint64_t events_total = 0;
    uint64_t activations_total = 0;
    uint64_t restorations_total = 0;
    int active_count = 0;
    bool has_last_event = false;
    AvoidanceEvent last_event;
};

struct ExecutionTaskSnapshot {
    int drone_id = 0;
    std::string mode;
    bool closed_loop = false;
    int current_wp = 0;
    int waypoint_count = 0;
    bool target_override = false;
    bool avoidance_active = false;
    double base_ned_n = 0.0;
    double base_ned_e = 0.0;
    double base_ned_d = 0.0;
    double target_ned_n = 0.0;
    double target_ned_e = 0.0;
    double target_ned_d = 0.0;
};

/// 执行引擎
///
/// 职责：
///   1. 接收集结完成信号，按模式启动各机执行线程
///   2. 侦察模式（scout）：按航点序列依次飞行，支持循环
///   3. 巡逻模式：侦察基础上支持目标识别事件中断
///   4. 攻击模式：按航点序列飞行，到达最后航点后悬停
///   5. 实时避障：监控所有在线无人机，预测碰撞并临时调整目标
class ExecutionEngine {
public:
    using MoveCallback = std::function<void(int drone_id, double ned_n, double ned_e, double ned_d)>;
    using TelemetryGetter = std::function<TelemetryData(int drone_id)>;
    using StateGetter = std::function<DroneConnectionState(int drone_id)>;
    using AvoidanceCallback = std::function<void(const AvoidanceEvent&)>;

    ExecutionEngine(double arrival_threshold_m,
                    double avoidance_radius_m,
                    double avoidance_lookahead_sec);
    ~ExecutionEngine();

    void SetMoveCallback(MoveCallback cb);
    void SetTelemetryGetter(TelemetryGetter cb);
    void SetStateGetter(StateGetter cb);
    void SetAvoidanceCallback(AvoidanceCallback cb);

    /// 启动所有任务（集结完成后调用）
    void StartTasks(const AssemblyConfig& config);

    /// 停止所有任务
    void StopAll();

    /// 更新无人机位置（由遥测回调驱动）
    void OnTelemetry(int drone_id, const TelemetryData& tel);

    /// 注入目标识别事件（巡逻模式）
    void InjectTarget(int drone_id, double ue_x, double ue_y, double ue_z);

    bool IsRunning() const { return running_; }
    std::vector<ExecutionTaskSnapshot> GetTaskSnapshots() const;
    AvoidanceStats GetAvoidanceStats() const;

    // ===== task_state callback =====
    using TaskStateCallback = std::function<void(int drone_id, 
                                                 const std::string& state,
                                                 const std::string& detail,
                                                 int current_wp, 
                                                 int total_wp)>;
    void SetTaskStateCallback(TaskStateCallback cb);

private:
    void RunDroneTask(int drone_id);
    bool WaitForArrival(int drone_id, double ned_n, double ned_e, double ned_d);
    void CheckAvoidance();
    void CheckAvoidanceLoop();
    void RestoreExpiredAvoidanceTargets(const std::chrono::steady_clock::time_point& now);

    double arrival_threshold_m_;
    double avoidance_radius_m_;
    double avoidance_lookahead_sec_;

    MoveCallback move_cb_;
    TelemetryGetter telemetry_getter_;
    StateGetter state_getter_;
    AvoidanceCallback avoidance_cb_;

    std::atomic<bool> running_{false};

    mutable std::mutex tasks_mutex_;
    std::unordered_map<int, DroneTask> tasks_;
    std::unordered_map<int, std::unique_ptr<std::thread>> threads_;

    // 避障：每机当前实际目标（NED）
    struct DroneTarget {
        double base_ned_n = 0, base_ned_e = 0, base_ned_d = 0;
        double ned_n = 0, ned_e = 0, ned_d = 0;
        bool avoidance_active = false;
        std::chrono::steady_clock::time_point avoidance_restore_at{};
        std::chrono::steady_clock::time_point avoidance_cooldown_until{};
    };
    mutable std::mutex targets_mutex_;
    std::unordered_map<int, DroneTarget> targets_;

    mutable std::mutex avoidance_stats_mutex_;
    AvoidanceStats avoidance_stats_;

    std::thread avoidance_thread_;

    TaskStateCallback task_state_cb_;
};
