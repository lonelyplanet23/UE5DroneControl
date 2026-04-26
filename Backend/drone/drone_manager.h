#pragma once

#include "core/types.h"
#include "drone_context.h"
#include "heartbeat_manager.h"
#include "conversion/gps_anchor_manager.h"
#include <memory>
#include <unordered_map>
#include <functional>

/// 多无人机管理器（全局单例）
///
/// 核心职责：
/// 1. 管理多架无人机的运行状态（DroneContext）
/// 2. 接收 UDP 遥测 → 更新状态、触发状态机、通知回调
/// 3. 接收控制指令 → 坐标转换 → 入队
/// 4. 超时监控
/// 5. 对外提供注册/查询接口
class DroneManager {
public:
    DroneManager(HeartbeatManager& hb_manager);
    ~DroneManager();

    // ========================================================
    // 初始化
    // ========================================================
    /// 注册回调（由 WS Server 调用）
    void SetTelemetryCallback(TelemetryCallback cb);
    void SetStateChangeCallback(StateChangeCallback cb);
    void SetAlertCallback(AlertCallback cb);
    void SetAssemblyCallback(AssemblyCallback cb);

    // ========================================================
    // 无人机管理（由 HTTP 注册接口调用）
    // ========================================================
    /// 添加无人机
    bool AddDrone(int drone_id,
                  int slot,
                  const std::string& name,
                  const std::string& jetson_ip = "",
                  int send_port = 0);

    /// 移除无人机
    bool RemoveDrone(int drone_id);

    /// 获取所有无人机状态
    std::vector<DroneStatus> GetAllStatus() const;

    /// 获取单个无人机状态
    DroneStatus GetStatus(int drone_id) const;

    // ========================================================
    // 遥测接收（由 UDP Receiver 调用）
    // ========================================================
    /// 收到遥测数据
    /// @param drone_id  无人机 ID（由端口映射确定）
    /// @param data      解析后的遥测数据
    void OnTelemetryReceived(int drone_id, const TelemetryData& data);

    // ========================================================
    // 控制指令（由 WS Server 收到 move/pause/resume 时调用）
    // ========================================================
    /// 处理移动指令
    /// @param ue_x, ue_y, ue_z  UE 偏移坐标（厘米）
    bool ProcessMoveCommand(int drone_id,
                            double ue_x, double ue_y, double ue_z);

    /// 处理暂停指令
    bool ProcessPauseCommand(int drone_id);

    /// 处理恢复指令
    bool ProcessResumeCommand(int drone_id);

    // ========================================================
    // 状态查询
    // ========================================================
    DroneConnectionState GetConnectionState(int drone_id) const;
    TelemetryData GetLatestTelemetry(int drone_id) const;
    GpsAnchor GetAnchor(int drone_id) const;
    bool HasDrone(int drone_id) const;
    bool GetQueueDebugInfo(int drone_id,
                           size_t& queue_size,
                           bool& paused,
                           DroneControlPacket* next_cmd = nullptr) const;

    // ========================================================
    // 超时检查（由监控线程每秒调用）
    // ========================================================
    void CheckTimeouts(int timeout_sec);

    // ========================================================
    // 锚点管理
    // ========================================================
    GpsAnchorManager& GetAnchorManager() { return anchor_manager_; }

private:
    DroneContext* GetContext(int drone_id);

    HeartbeatManager& hb_manager_;
    std::unordered_map<int, std::unique_ptr<DroneContext>> drones_;
    GpsAnchorManager anchor_manager_;

    // 回调（WS Server 注册）
    TelemetryCallback      telemetry_cb_;
    StateChangeCallback    state_change_cb_;
    AlertCallback          alert_cb_;
    AssemblyCallback       assembly_cb_;
};
