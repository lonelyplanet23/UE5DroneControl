#include "drone_manager.h"
#include "conversion/coordinate_converter.h"
#include "conversion/quaternion_utils.h"
#include <spdlog/spdlog.h>
#include <algorithm>

DroneManager::DroneManager(HeartbeatManager& hb_manager)
    : hb_manager_(hb_manager)
{
    spdlog::info("DroneManager created");
}

DroneManager::~DroneManager()
{
    spdlog::info("DroneManager destroyed");
}

// ========================================================
// 回调注册
// ========================================================
void DroneManager::SetTelemetryCallback(TelemetryCallback cb)
{
    telemetry_cb_ = std::move(cb);
}
void DroneManager::SetStateChangeCallback(StateChangeCallback cb)
{
    state_change_cb_ = std::move(cb);
}
void DroneManager::SetAlertCallback(AlertCallback cb)
{
    alert_cb_ = std::move(cb);
}
void DroneManager::SetAssemblyCallback(AssemblyCallback cb)
{
    assembly_cb_ = std::move(cb);
}

// ========================================================
// 无人机管理
// ========================================================
bool DroneManager::AddDrone(int drone_id,
                            int slot,
                            const std::string& name,
                            const std::string& jetson_ip,
                            int send_port)
{
    if (drones_.find(drone_id) != drones_.end()) {
        spdlog::warn("Drone {} already exists", drone_id);
        return false;
    }

    auto ctx = std::make_unique<DroneContext>(drone_id, slot, name);
    if (!jetson_ip.empty()) {
        ctx->jetson_ip = jetson_ip;
    }
    if (send_port > 0) {
        ctx->send_port = send_port;
    }

    // 状态变更回调 → 起停心跳 + WS 推送
    // 注意：start_heartbeat/stop_hbard 通过 hb_manager 执行
    ctx->state_machine = std::make_unique<StateMachine>(
        [this, drone_id](StateEvent event) {
            auto* ctx = GetContext(drone_id);
            if (!ctx) return;

            switch (event) {
                case StateEvent::PowerOn:
                case StateEvent::Reconnect:
                    // 通知 WS
                    if (state_change_cb_) {
                        auto anchor = anchor_manager_.GetAnchor(drone_id);
                        state_change_cb_(drone_id, event, anchor);
                    }
                    // [修复 #1]: 启动心跳，传入指令队列消费回调
                    hb_manager_.Start(drone_id, ctx->jetson_ip, ctx->send_port,
                        [this, drone_id](DroneControlPacket& cmd) -> bool {
                            auto* q_ctx = GetContext(drone_id);
                            if (!q_ctx) return false;
                            return q_ctx->command_queue->Pop(cmd);
                        });
                    break;

                case StateEvent::LostConnection:
                    // 停止心跳
                    hb_manager_.Stop(drone_id);
                    // 通知 WS
                    if (state_change_cb_) {
                        state_change_cb_(drone_id, event, GpsAnchor{});
                    }
                    if (alert_cb_) {
                        alert_cb_(drone_id, "lost_connection", 0);
                    }
                    break;
            }
        });

    drones_[drone_id] = std::move(ctx);
    spdlog::info("Drone {} (slot={}) added: {}", drone_id, slot, name);
    return true;
}

bool DroneManager::RemoveDrone(int drone_id)
{
    auto it = drones_.find(drone_id);
    if (it == drones_.end()) return false;

    hb_manager_.Stop(drone_id);
    anchor_manager_.ClearAnchor(drone_id);
    drones_.erase(it);
    spdlog::info("Drone {} removed", drone_id);
    return true;
}

std::vector<DroneStatus> DroneManager::GetAllStatus() const
{
    std::vector<DroneStatus> result;
    for (const auto& [id, ctx] : drones_) {
        result.push_back(GetStatus(id));
    }
    return result;
}

DroneStatus DroneManager::GetStatus(int drone_id) const
{
    DroneStatus s;
    auto it = drones_.find(drone_id);
    if (it == drones_.end()) return s;

    const auto& ctx = it->second;
    s.id     = "d" + std::to_string(drone_id);
    s.name   = ctx->name;
    s.slot   = ctx->slot;
    s.battery = ctx->has_telemetry ? ctx->latest_telemetry.battery : -1;

    switch (ctx->state_machine->GetState()) {
        case DroneConnectionState::Offline:    s.connection_state = "offline"; break;
        case DroneConnectionState::Connecting: s.connection_state = "connecting"; break;
        case DroneConnectionState::Online:     s.connection_state = "online"; break;
        case DroneConnectionState::Lost:       s.connection_state = "lost"; break;
    }

    if (ctx->has_telemetry) {
        const auto& tel = ctx->latest_telemetry;
        CoordinateConverter::NedToUeOffset(
            tel.position_ned[0], tel.position_ned[1], tel.position_ned[2],
            s.pos_x, s.pos_y, s.pos_z);
        s.yaw   = QuaternionUtils::GetUeYaw(tel.quaternion[0], tel.quaternion[1],
                                              tel.quaternion[2], tel.quaternion[3]);
        s.speed = QuaternionUtils::SpeedFromVelocity(
            tel.velocity[0], tel.velocity[1], tel.velocity[2]);
    }

    s.anchor = anchor_manager_.GetAnchor(drone_id);
    return s;
}

DroneContext* DroneManager::GetContext(int drone_id)
{
    auto it = drones_.find(drone_id);
    return (it != drones_.end()) ? it->second.get() : nullptr;
}

// ========================================================
// 遥测接收
// ========================================================
void DroneManager::OnTelemetryReceived(int drone_id, const TelemetryData& data)
{
    auto ctx = GetContext(drone_id);
    if (!ctx) {
        spdlog::warn("Telemetry for unknown drone {}", drone_id);
        return;
    }

    // 1. 更新遥测缓存
    ctx->latest_telemetry = data;
    ctx->has_telemetry = true;

    // 2. 更新心跳最后位置（从 NED 位置）
    hb_manager_.UpdateLastPosition(drone_id,
        data.position_ned[0], data.position_ned[1], data.position_ned[2]);

    // 3. 如果 GPS 有效，先记录锚点，再推进状态机。
    // 这样首包 power_on / reconnect 事件才能带上最新 anchor。
    if (data.gps_fix) {
        anchor_manager_.SetAnchor(
            drone_id, data.gps_lat, data.gps_lon, data.gps_alt);
    }

    // 4. 状态机推进（可能触发 PowerOn / Reconnect / LostConnection）
    ctx->state_machine->OnTelemetryReceived();

    // 5. 低电量告警
    if (data.battery >= 0 && data.battery <= 20 && alert_cb_) {
        alert_cb_(drone_id, "low_battery", data.battery);
    }

    // 6. 推送遥测到 WebSocket
    if (telemetry_cb_) {
        telemetry_cb_(drone_id, data);
    }
}

// ========================================================
// 控制指令
// ========================================================
bool DroneManager::ProcessMoveCommand(int drone_id,
                                       double ue_x, double ue_y, double ue_z)
{
    auto ctx = GetContext(drone_id);
    if (!ctx) return false;

    // UE 偏移厘米 → NED 米
    double ned_n, ned_e, ned_d;
    CoordinateConverter::UeOffsetToNed(ue_x, ue_y, ue_z, ned_n, ned_e, ned_d);

    // 保存最后 NED 位置
    ctx->last_ned_x = ned_n;
    ctx->last_ned_y = ned_e;
    ctx->last_ned_z = ned_d;

    // 更新心跳位置
    hb_manager_.UpdateLastPosition(drone_id, ned_n, ned_e, ned_d);

    // 构建控制包
    DroneControlPacket cmd;
    cmd.timestamp = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()) / 1000000.0;
    cmd.x = static_cast<float>(ned_n);
    cmd.y = static_cast<float>(ned_e);
    cmd.z = static_cast<float>(ned_d);
    cmd.mode = 1;  // 移动

    ctx->command_queue->Push(cmd);
    spdlog::debug("MoveCommand drone={}: NED({:.2f}, {:.2f}, {:.2f})",
                  drone_id, ned_n, ned_e, ned_d);
    return true;
}

bool DroneManager::ProcessPauseCommand(int drone_id)
{
    auto ctx = GetContext(drone_id);
    if (!ctx) return false;

    ctx->command_queue->SetPaused(true);

    // 发送悬停包
    DroneControlPacket cmd;
    cmd.timestamp = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()) / 1000000.0;
    cmd.x = static_cast<float>(ctx->last_ned_x);
    cmd.y = static_cast<float>(ctx->last_ned_y);
    cmd.z = static_cast<float>(ctx->last_ned_z);
    cmd.mode = 0;

    ctx->command_queue->Push(cmd);
    spdlog::info("Pause drone {}", drone_id);
    return true;
}

bool DroneManager::ProcessResumeCommand(int drone_id)
{
    auto ctx = GetContext(drone_id);
    if (!ctx) return false;

    ctx->command_queue->SetPaused(false);
    spdlog::info("Resume drone {}", drone_id);
    return true;
}

// ========================================================
// 状态查询
// ========================================================
DroneConnectionState DroneManager::GetConnectionState(int drone_id) const
{
    auto it = drones_.find(drone_id);
    if (it == drones_.end()) return DroneConnectionState::Offline;
    return it->second->state_machine->GetState();
}

TelemetryData DroneManager::GetLatestTelemetry(int drone_id) const
{
    auto it = drones_.find(drone_id);
    if (it == drones_.end()) return TelemetryData{};
    return it->second->latest_telemetry;
}

GpsAnchor DroneManager::GetAnchor(int drone_id) const
{
    return anchor_manager_.GetAnchor(drone_id);
}

bool DroneManager::HasDrone(int drone_id) const
{
    return drones_.find(drone_id) != drones_.end();
}

bool DroneManager::GetQueueDebugInfo(int drone_id,
                                     size_t& queue_size,
                                     bool& paused,
                                     DroneControlPacket* next_cmd) const
{
    auto it = drones_.find(drone_id);
    if (it == drones_.end() || !it->second || !it->second->command_queue) {
        return false;
    }

    const auto& queue = it->second->command_queue;
    queue_size = queue->Size();
    paused = queue->IsPaused();

    if (next_cmd) {
        DroneControlPacket peeked{};
        if (queue->Peek(peeked)) {
            *next_cmd = peeked;
        } else {
            *next_cmd = DroneControlPacket{};
        }
    }

    return true;
}

// ========================================================
// 超时检查
// ========================================================
void DroneManager::CheckTimeouts(int timeout_sec)
{
    for (auto& [id, ctx] : drones_) {
        ctx->state_machine->CheckTimeout(timeout_sec);
    }
}
