#include "drone_manager.h"

#include "conversion/coordinate_converter.h"
#include "conversion/quaternion_utils.h"

#include <chrono>
#include <cmath>
#include <spdlog/spdlog.h>

namespace {

double now_unix_seconds()
{
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()) / 1000000.0;
}

DroneControlPacket make_packet(int slot, double ned_n, double ned_e,
                               double ned_d, uint32_t mode)
{
    DroneControlPacket cmd{};
    cmd.sequence = NextControlSequence();
    cmd.timestamp = now_unix_seconds();
    cmd.x = static_cast<float>(ned_n);
    cmd.y = static_cast<float>(ned_e);
    cmd.z = static_cast<float>(ned_d);
    cmd.mode = mode;
    cmd.slot = slot;
    return cmd;
}

bool is_valid_gps_anchor(const TelemetryData& data)
{
    return data.gps_fix
        && std::isfinite(data.gps_lat)
        && std::isfinite(data.gps_lon)
        && std::isfinite(data.gps_alt)
        && data.gps_lat >= -90.0 && data.gps_lat <= 90.0
        && data.gps_lon >= -180.0 && data.gps_lon <= 180.0;
}

bool has_fresh_valid_local_position(const DroneContext& ctx)
{
    constexpr double kMaxSafeHoldTelemetryAgeSeconds = 3.0;
    const double age_seconds = now_unix_seconds() - ctx.last_telemetry_unix;
    return ctx.has_telemetry
        && ctx.latest_telemetry.local_position_valid
        && std::isfinite(ctx.latest_telemetry.position_ned[0])
        && std::isfinite(ctx.latest_telemetry.position_ned[1])
        && std::isfinite(ctx.latest_telemetry.position_ned[2])
        && ctx.last_telemetry_unix > 0.0
        && age_seconds >= 0.0
        && age_seconds <= kMaxSafeHoldTelemetryAgeSeconds;
}

} // namespace

DroneManager::DroneManager(HeartbeatManager& hb_manager, int low_battery_threshold)
    : hb_manager_(hb_manager)
    , low_battery_threshold_(low_battery_threshold)
{
    spdlog::info("DroneManager created");
}

DroneManager::~DroneManager()
{
    spdlog::info("DroneManager destroyed");
}

void DroneManager::SetTelemetryCallback(TelemetryCallback cb)   { telemetry_cb_ = std::move(cb); }
void DroneManager::SetStateChangeCallback(StateChangeCallback cb) { state_change_cb_ = std::move(cb); }
void DroneManager::SetAlertCallback(AlertCallback cb)           { alert_cb_ = std::move(cb); }
void DroneManager::SetAssemblyCallback(AssemblyCallback cb)     { assembly_cb_ = std::move(cb); }

// ========================================================
// 无人机管理（线程安全）
// ========================================================
bool DroneManager::AddDrone(int drone_id, int slot, const std::string& name,
                            const std::string& jetson_ip, int send_port)
{
    std::lock_guard<std::mutex> lock(drones_mutex_);

    if (drones_.find(drone_id) != drones_.end()) {
        spdlog::warn("Drone {} already exists", drone_id);
        return false;
    }
    if (GetContextBySlot(slot) != nullptr) {
        spdlog::warn("Slot {} already has a registered drone", slot);
        return false;
    }

    auto ctx = std::make_unique<DroneContext>(drone_id, slot, name);
    CommandQueue* command_queue = ctx->command_queue.get();
    if (!jetson_ip.empty()) {
        ctx->jetson_ip = jetson_ip;
    }
    ctx->send_port = send_port > 0 ? send_port : 8889 + (slot - 1) * 2;

    ctx->state_machine = std::make_unique<StateMachine>(
        [this, drone_id, command_queue](StateEvent event) {
            // NOTE: 此回调在持有 drones_mutex_ 的线程上被触发，
            //       必须用 GetContextUnsafe（不加锁）而非 GetContext。
            auto* ctx = GetContextUnsafe(drone_id);
            if (!ctx) return;

            switch (event) {
                case StateEvent::PowerOn:
                case StateEvent::Reconnect: {
                    // This transition is driven only by an estimator-valid
                    // VehicleLocalPosition sample. Hold that measured NED point,
                    // never the guessed PX4 local origin.
                    const double hold_x = ctx->latest_telemetry.position_ned[0];
                    const double hold_y = ctx->latest_telemetry.position_ned[1];
                    const double hold_z = ctx->latest_telemetry.position_ned[2];
                    if (event == StateEvent::Reconnect) {
                        // Do not replay commands from the previous connection.
                        ctx->command_queue->Clear();
                        spdlog::warn(
                            "Drone {} reconnected: cleared stale commands and holding current NED",
                            drone_id);
                    }
                    ctx->last_ned_x = hold_x;
                    ctx->last_ned_y = hold_y;
                    ctx->last_ned_z = hold_z;
                    if (state_change_cb_) {
                        state_change_cb_(drone_id, event, anchor_manager_.GetAnchor(drone_id));
                    }
                    hb_manager_.Start(
                        drone_id, ctx->slot, ctx->jetson_ip, ctx->send_port,
                        [command_queue](DroneControlPacket& cmd) -> bool {
                            // The queue owns its own mutex and remains alive until
                            // RemoveDrone stops and joins this heartbeat thread.
                            // Avoid taking drones_mutex_ here: LostConnection is
                            // emitted while that mutex is held and synchronously
                            // stops the heartbeat, so reacquiring it would deadlock.
                            return command_queue && command_queue->Pop(cmd);
                        },
                        hold_x, hold_y, hold_z);
                    break;
                }

                case StateEvent::LostConnection:
                    hb_manager_.Stop(drone_id);
                    ctx->command_queue->Clear();
                    ctx->last_ned_x = 0.0;
                    ctx->last_ned_y = 0.0;
                    ctx->last_ned_z = 0.0;
                    // A reconnect is treated as a new power-on lifecycle: the
                    // PX4 local origin and its GPS anchor must both be rebuilt.
                    anchor_manager_.ClearAnchor(drone_id);
                    ctx->latest_telemetry.local_position_valid = false;
                    ctx->has_telemetry = false;
                    ctx->last_telemetry_unix = 0.0;
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
    spdlog::info("Drone {} added: slot={} name={}", drone_id, slot, name);
    return true;
}

bool DroneManager::RemoveDrone(int drone_id)
{
    std::lock_guard<std::mutex> lock(drones_mutex_);

    auto it = drones_.find(drone_id);
    if (it == drones_.end()) return false;

    hb_manager_.Stop(drone_id);
    anchor_manager_.ClearAnchor(drone_id);
    drones_.erase(it);
    spdlog::info("Drone {} removed", drone_id);
    return true;
}

bool DroneManager::HasDrone(int drone_id) const
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    return drones_.find(drone_id) != drones_.end();
}

std::vector<DroneStatus> DroneManager::GetAllStatus() const
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    std::vector<DroneStatus> result;
    result.reserve(drones_.size());
    for (const auto& [id, ctx] : drones_) {
        result.push_back(GetStatusInternal(id));
    }
    return result;
}

DroneStatus DroneManager::GetStatus(int drone_id) const
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    return GetStatusInternal(drone_id);
}

DroneStatus DroneManager::GetStatusInternal(int drone_id) const
{
    DroneStatus s;
    auto it = drones_.find(drone_id);
    if (it == drones_.end()) return s;

    const auto& ctx = it->second;
    s.id = "d" + std::to_string(drone_id);
    s.name = ctx->name;
    s.slot = ctx->slot;
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
        s.yaw = QuaternionUtils::GetUeYaw(
            tel.quaternion[0], tel.quaternion[1], tel.quaternion[2], tel.quaternion[3]);
        s.speed = QuaternionUtils::SpeedFromVelocity(
            tel.velocity[0], tel.velocity[1], tel.velocity[2]);
    }
    s.anchor = anchor_manager_.GetAnchor(drone_id);
    if (ctx->has_telemetry) {
        s.control_ack_command_id = ctx->latest_telemetry.control_ack_command_id;
        s.control_ack_sequence = ctx->latest_telemetry.control_ack_sequence;
    }
    s.task_state = ctx->task_state;
    s.task_error_detail = ctx->task_error_detail;
    s.task_current_wp = ctx->task_current_wp;
    s.task_total_wp = ctx->task_total_wp;
    return s;
}

std::vector<DroneControlPacket> DroneManager::GetCommandQueueSnapshot(int drone_id) const
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto it = drones_.find(drone_id);
    if (it == drones_.end() || !it->second->command_queue) return {};
    return it->second->command_queue->Snapshot();
}

bool DroneManager::IsCommandQueuePaused(int drone_id) const
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto it = drones_.find(drone_id);
    return it != drones_.end() && it->second->command_queue && it->second->command_queue->IsPaused();
}

size_t DroneManager::GetCommandQueueSize(int drone_id) const
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto it = drones_.find(drone_id);
    return (it != drones_.end() && it->second->command_queue) ? it->second->command_queue->Size() : 0;
}

HeartbeatStats DroneManager::GetHeartbeatStats(int drone_id) const
{
    return hb_manager_.GetStats(drone_id);
}

// ========================================================
// 遥测接收
// ========================================================
void DroneManager::OnTelemetryReceived(int drone_id, const TelemetryData& data)
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto* ctx = GetContextUnsafe(drone_id);
    if (!ctx) {
        spdlog::warn("Telemetry for unknown drone {}", drone_id);
        return;
    }
    HandleTelemetry(*ctx, data);
}

void DroneManager::OnTelemetryReceivedBySlot(int slot, const TelemetryData& data)
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto* ctx = GetContextBySlot(slot);
    if (!ctx) {
        spdlog::warn("Telemetry for unregistered slot {}", slot);
        return;
    }
    HandleTelemetry(*ctx, data);
}

int DroneManager::ResolveDroneIdBySlot(int slot) const
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    const auto* ctx = GetContextBySlot(slot);
    return ctx ? ctx->drone_id : 0;
}

// ========================================================
// 控制指令
// ========================================================
bool DroneManager::ProcessMoveCommand(int drone_id, double ue_x, double ue_y, double ue_z)
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto* ctx = GetContextUnsafe(drone_id);
    if (!ctx) return false;

    double ned_n, ned_e, ned_d;
    CoordinateConverter::UeOffsetToNed(ue_x, ue_y, ue_z, ned_n, ned_e, ned_d);

    ctx->last_ned_x = ned_n;  ctx->last_ned_y = ned_e;  ctx->last_ned_z = ned_d;
    const auto packet = make_packet(ctx->slot, ned_n, ned_e, ned_d, 1);
    ctx->command_queue->Push(packet);
    spdlog::info(
        "MoveCommand drone={} sequence={}: UE-relative(cm)=({:.2f},{:.2f},{:.2f}) "
        "-> power-on-relative NED(m)=({:.3f},{:.3f},{:.3f})",
        drone_id, packet.sequence, ue_x, ue_y, ue_z, ned_n, ned_e, ned_d);
    return true;
}

bool DroneManager::ProcessMoveCommandNed(int drone_id, double ned_n, double ned_e, double ned_d)
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto* ctx = GetContextUnsafe(drone_id);
    if (!ctx) return false;

    ctx->last_ned_x = ned_n;  ctx->last_ned_y = ned_e;  ctx->last_ned_z = ned_d;
    const auto packet = make_packet(ctx->slot, ned_n, ned_e, ned_d, 1);
    ctx->command_queue->Push(packet);
    spdlog::info(
        "MoveCommandNed drone={} sequence={}: power-on-relative "
        "NED(m)=({:.3f},{:.3f},{:.3f})",
        drone_id, packet.sequence, ned_n, ned_e, ned_d);
    return true;
}

bool DroneManager::ProcessPauseCommand(int drone_id)
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto* ctx = GetContextUnsafe(drone_id);
    if (!ctx) return false;

    ctx->command_queue->SetPaused(true);
    // 暂停必须保持“当前实测位置”，不能继续保持尚未到达的旧目标点。
    if (ctx->has_telemetry) {
        ctx->last_ned_x = ctx->latest_telemetry.position_ned[0];
        ctx->last_ned_y = ctx->latest_telemetry.position_ned[1];
        ctx->last_ned_z = ctx->latest_telemetry.position_ned[2];
    }
    ctx->previous_task_state = ctx->task_state;
    SetDroneTaskState(drone_id, "paused", "", ctx->task_current_wp, ctx->task_total_wp);
    hb_manager_.RequestHold(
        drone_id, ctx->last_ned_x, ctx->last_ned_y, ctx->last_ned_z);
    spdlog::info(
        "Pause drone {} at measured power-on-relative NED({:.3f},{:.3f},{:.3f})",
        drone_id, ctx->last_ned_x, ctx->last_ned_y, ctx->last_ned_z);
    return true;
}

bool DroneManager::ProcessResumeCommand(int drone_id)
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto* ctx = GetContextUnsafe(drone_id);
    if (!ctx) return false;

    ctx->command_queue->SetPaused(false);
    SetDroneTaskState(drone_id, ctx->previous_task_state, "",
                  ctx->task_current_wp, ctx->task_total_wp);
    spdlog::info("Resume drone {}", drone_id);
    return true;
}

DroneConnectionState DroneManager::GetConnectionState(int drone_id) const
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto it = drones_.find(drone_id);
    return (it != drones_.end()) ? it->second->state_machine->GetState() : DroneConnectionState::Offline;
}

TelemetryData DroneManager::GetLatestTelemetry(int drone_id) const
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto it = drones_.find(drone_id);
    return (it != drones_.end()) ? it->second->latest_telemetry : TelemetryData{};
}

GpsAnchor DroneManager::GetAnchor(int drone_id) const
{
    return anchor_manager_.GetAnchor(drone_id);
}

void DroneManager::CheckTimeouts(int timeout_sec)
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    for (auto& [id, ctx] : drones_) {
        ctx->state_machine->CheckTimeout(timeout_sec);
    }
}

std::vector<int> DroneManager::RefreshDisconnectedConnections()
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    std::vector<int> refreshed_ids;

    for (auto& [id, ctx] : drones_) {
        const auto state = ctx->state_machine->GetState();
        if (state == DroneConnectionState::Online) {
            continue;
        }

        // Refresh must not start a heartbeat at a guessed local origin.
        if (!has_fresh_valid_local_position(*ctx)) {
            spdlog::warn(
                "[ConnectionRefresh] drone {} has no fresh valid PX4 local position; skipped",
                id);
            continue;
        }
        const double hold_x = ctx->latest_telemetry.position_ned[0];
        const double hold_y = ctx->latest_telemetry.position_ned[1];
        const double hold_z = ctx->latest_telemetry.position_ned[2];

        hb_manager_.Start(id, ctx->slot, ctx->jetson_ip, ctx->send_port,
            [queue = ctx->command_queue.get()](DroneControlPacket& cmd) -> bool {
                return queue && queue->Pop(cmd);
            },
            hold_x, hold_y, hold_z);
        hb_manager_.RequestHold(id, hold_x, hold_y, hold_z);
        refreshed_ids.push_back(id);

        spdlog::info(
            "[ConnectionRefresh] probing drone {} state={} -> {}:{} with safe hold",
            id,
            state == DroneConnectionState::Lost ? "lost" : "offline",
            ctx->jetson_ip, ctx->send_port);
    }

    return refreshed_ids;
}

// ========================================================
// 内部辅助（不加锁）
// ========================================================
DroneContext* DroneManager::GetContext(int drone_id)
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    return GetContextUnsafe(drone_id);
}

const DroneContext* DroneManager::GetContext(int drone_id) const
{
    std::lock_guard<std::mutex> lock(drones_mutex_);
    return GetContextUnsafe(drone_id);
}

DroneContext* DroneManager::GetContextBySlot(int slot)
{
    for (auto& [id, ctx] : drones_) {
        if (ctx && ctx->slot == slot) return ctx.get();
    }
    return nullptr;
}

const DroneContext* DroneManager::GetContextBySlot(int slot) const
{
    for (const auto& [id, ctx] : drones_) {
        if (ctx && ctx->slot == slot) return ctx.get();
    }
    return nullptr;
}

DroneContext* DroneManager::GetContextUnsafe(int drone_id) const
{
    auto it = drones_.find(drone_id);
    return (it != drones_.end()) ? it->second.get() : nullptr;
}

// ========================================================
// 遥测处理（调用者持有锁）
// ========================================================
void DroneManager::HandleTelemetry(DroneContext& ctx, const TelemetryData& data)
{
    const int drone_id = ctx.drone_id;
    const DroneConnectionState state_before = ctx.state_machine->GetState();
    const bool had_anchor = anchor_manager_.HasAnchor(drone_id);
    const bool local_position_valid = data.local_position_valid
        && std::isfinite(data.position_ned[0])
        && std::isfinite(data.position_ned[1])
        && std::isfinite(data.position_ned[2]);

    ctx.latest_telemetry = data;
    ctx.latest_telemetry.local_position_valid = local_position_valid;
    ctx.has_telemetry = true;
    ctx.last_telemetry_unix = now_unix_seconds();

    bool created_anchor = false;
    if (!had_anchor && is_valid_gps_anchor(data)) {
        anchor_manager_.SetAnchor(drone_id, data.gps_lat, data.gps_lon, data.gps_alt);
        created_anchor = true;
        spdlog::debug("[DroneManager] Drone {} power-on GPS anchor: ({:.6f},{:.6f},{:.1f}m)",
                      drone_id, data.gps_lat, data.gps_lon, data.gps_alt);
    }

    // Starting the heartbeat from odometry in a different pose frame, or from
    // an invalid local estimate, can command (0,0,0) while airborne.
    if (local_position_valid) {
        ctx.state_machine->OnTelemetryReceived();
    }

    // Local NED can become valid before GPS. Publish the frozen anchor once
    // the first valid GPS tuple arrives so UE can enable geographic dispatch.
    if (created_anchor && state_before == DroneConnectionState::Online && state_change_cb_) {
        state_change_cb_(drone_id, StateEvent::PowerOn, anchor_manager_.GetAnchor(drone_id));
    }

    // 遥测位置是“当前实际位置”，不能覆盖后端的“期望目标位置”。
    // 否则 move 只发一帧，下一帧 heartbeat 就会把目标改成当前点，导致飞机停住。
    if (data.control_ack_sequence > ctx.last_logged_control_ack_sequence) {
        ctx.last_logged_control_ack_sequence = data.control_ack_sequence;
        spdlog::info(
            "[ControlAck] drone={} session={} command_id={} sequence={} "
            "mode={} confirmed_packets={}",
            drone_id, data.control_ack_session_id,
            data.control_ack_command_id, data.control_ack_sequence,
            data.control_ack_mode, data.control_ack_confirmed_packets);
    }

    if (data.battery >= 0 && data.battery > low_battery_threshold_) {
        ctx.low_battery_alert_active = false;
    }
    if (data.battery >= 0 && data.battery <= low_battery_threshold_ &&
        !ctx.low_battery_alert_active && alert_cb_) {
        ctx.low_battery_alert_active = true;
        spdlog::debug("[DroneManager] Drone {} low battery alert triggered: {}%",
                      drone_id, data.battery);
        alert_cb_(drone_id, "low_battery", data.battery);
    }

    if (telemetry_cb_) {
        telemetry_cb_(drone_id, ctx.latest_telemetry);
    }
}

void DroneManager::SetTaskStateCallback(TaskStateCallback cb) {
    task_state_cb_ = std::move(cb);
}

void DroneManager::SetDroneTaskState(int drone_id, const std::string& state,
                                     const std::string& detail,
                                     int current_wp, int total_wp) {
    std::lock_guard<std::mutex> lock(drones_mutex_);
    auto* ctx = GetContextUnsafe(drone_id);
    if (!ctx) return;
    
    ctx->task_state = state;
    ctx->task_error_detail = detail;
    ctx->task_current_wp = current_wp;
    ctx->task_total_wp = total_wp;
    
    if (task_state_cb_) {
        task_state_cb_(drone_id, state, detail, current_wp, total_wp);
    }
}
