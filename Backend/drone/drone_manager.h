#pragma once

#include "conversion/gps_anchor_manager.h"
#include "core/types.h"
#include "drone_context.h"
#include "heartbeat_manager.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class DroneManager {
public:
    explicit DroneManager(HeartbeatManager& hb_manager, int low_battery_threshold = 20);
    ~DroneManager();

    void SetTelemetryCallback(TelemetryCallback cb);
    void SetStateChangeCallback(StateChangeCallback cb);
    void SetAlertCallback(AlertCallback cb);
    void SetAssemblyCallback(AssemblyCallback cb);

    bool AddDrone(int drone_id, int slot, const std::string& name,
                  const std::string& jetson_ip = "", int send_port = 0);
    bool RemoveDrone(int drone_id);
    bool HasDrone(int drone_id) const;

    std::vector<DroneStatus> GetAllStatus() const;
    DroneStatus GetStatus(int drone_id) const;

    std::vector<DroneControlPacket> GetCommandQueueSnapshot(int drone_id) const;
    bool IsCommandQueuePaused(int drone_id) const;
    size_t GetCommandQueueSize(int drone_id) const;
    HeartbeatStats GetHeartbeatStats(int drone_id) const;

    void OnTelemetryReceived(int drone_id, const TelemetryData& data);
    void OnTelemetryReceivedBySlot(int slot, const TelemetryData& data);
    int ResolveDroneIdBySlot(int slot) const;

    bool ProcessMoveCommand(int drone_id, double ue_x, double ue_y, double ue_z);
    bool ProcessMoveCommandNed(int drone_id, double ned_n, double ned_e, double ned_d);
    bool ProcessPauseCommand(int drone_id);
    bool ProcessResumeCommand(int drone_id);

    DroneConnectionState GetConnectionState(int drone_id) const;
    TelemetryData GetLatestTelemetry(int drone_id) const;
    GpsAnchor GetAnchor(int drone_id) const;

    void CheckTimeouts(int timeout_sec);

    // UDP 是无连接协议。对 Offline/Lost 设备重新发送安全 hold 心跳，
    // 让 Jetson 能重新发现后端；真正恢复 Online 仍以收到遥测为准。
    std::vector<int> RefreshDisconnectedConnections();

    GpsAnchorManager& GetAnchorManager() { return anchor_manager_; }

    //===== task_state callback =====
    using TaskStateCallback = std::function<void(int drone_id, 
                                                 const std::string& state,
                                                 const std::string& detail,
                                                 int current_wp, 
                                                 int total_wp)>;
    void SetTaskStateCallback(TaskStateCallback cb);
    void SetDroneTaskState(int drone_id, const std::string& state,
                           const std::string& detail = "",
                           int current_wp = 0, int total_wp = 0);

private:
    DroneContext* GetContext(int drone_id);
    const DroneContext* GetContext(int drone_id) const;
    DroneContext* GetContextUnsafe(int drone_id) const;
    DroneContext* GetContextBySlot(int slot);
    const DroneContext* GetContextBySlot(int slot) const;
    DroneStatus GetStatusInternal(int drone_id) const;
    void HandleTelemetry(DroneContext& ctx, const TelemetryData& data);

    HeartbeatManager& hb_manager_;
    int low_battery_threshold_ = 20;
    mutable std::mutex drones_mutex_;
    std::unordered_map<int, std::unique_ptr<DroneContext>> drones_;
    GpsAnchorManager anchor_manager_;

    TelemetryCallback telemetry_cb_;
    StateChangeCallback state_change_cb_;
    AlertCallback alert_cb_;
    AssemblyCallback assembly_cb_;

    TaskStateCallback task_state_cb_;
};
