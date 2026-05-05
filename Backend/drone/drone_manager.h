#pragma once

#include "conversion/gps_anchor_manager.h"
#include "core/types.h"
#include "drone_context.h"
#include "heartbeat_manager.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class DroneManager {
public:
    explicit DroneManager(HeartbeatManager& hb_manager);
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
    bool ProcessPauseCommand(int drone_id);
    bool ProcessResumeCommand(int drone_id);

    DroneConnectionState GetConnectionState(int drone_id) const;
    TelemetryData GetLatestTelemetry(int drone_id) const;
    GpsAnchor GetAnchor(int drone_id) const;

    void CheckTimeouts(int timeout_sec);

    GpsAnchorManager& GetAnchorManager() { return anchor_manager_; }

private:
    DroneContext* GetContext(int drone_id);
    const DroneContext* GetContext(int drone_id) const;
    DroneContext* GetContextBySlot(int slot);
    const DroneContext* GetContextBySlot(int slot) const;
    void HandleTelemetry(DroneContext& ctx, const TelemetryData& data);

    HeartbeatManager& hb_manager_;
    std::unordered_map<int, std::unique_ptr<DroneContext>> drones_;
    GpsAnchorManager anchor_manager_;

    TelemetryCallback telemetry_cb_;
    StateChangeCallback state_change_cb_;
    AlertCallback alert_cb_;
    AssemblyCallback assembly_cb_;
};
