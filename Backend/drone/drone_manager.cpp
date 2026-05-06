#include "drone_manager.h"

#include "conversion/coordinate_converter.h"
#include "conversion/quaternion_utils.h"

#include <chrono>
#include <spdlog/spdlog.h>

namespace {

double now_unix_seconds()
{
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()) / 1000000.0;
}

DroneControlPacket make_packet(double ned_n, double ned_e, double ned_d, uint32_t mode)
{
    DroneControlPacket cmd{};
    cmd.timestamp = now_unix_seconds();
    cmd.x = static_cast<float>(ned_n);
    cmd.y = static_cast<float>(ned_e);
    cmd.z = static_cast<float>(ned_d);
    cmd.mode = mode;
    return cmd;
}

} // namespace

DroneManager::DroneManager(HeartbeatManager& hb_manager)
    : hb_manager_(hb_manager)
{
    spdlog::info("DroneManager created");
}

DroneManager::~DroneManager()
{
    spdlog::info("DroneManager destroyed");
}

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

bool DroneManager::AddDrone(int drone_id, int slot, const std::string& name,
                            const std::string& jetson_ip, int send_port)
{
    if (drones_.find(drone_id) != drones_.end()) {
        spdlog::warn("Drone {} already exists", drone_id);
        return false;
    }
    if (GetContextBySlot(slot) != nullptr) {
        spdlog::warn("Slot {} already has a registered drone", slot);
        return false;
    }

    auto ctx = std::make_unique<DroneContext>(drone_id, slot, name);
    if (!jetson_ip.empty()) {
        ctx->jetson_ip = jetson_ip;
    }
    ctx->send_port = send_port > 0 ? send_port : 8889 + (slot - 1) * 2;

    ctx->state_machine = std::make_unique<StateMachine>(
        [this, drone_id](StateEvent event) {
            auto* ctx = GetContext(drone_id);
            if (!ctx) return;

            switch (event) {
                case StateEvent::PowerOn:
                case StateEvent::Reconnect:
                    if (state_change_cb_) {
                        state_change_cb_(drone_id, event, anchor_manager_.GetAnchor(drone_id));
                    }
                    hb_manager_.Start(drone_id, ctx->jetson_ip, ctx->send_port,
                        [this, drone_id](DroneControlPacket& cmd) -> bool {
                            auto* q_ctx = GetContext(drone_id);
                            return q_ctx && q_ctx->command_queue->Pop(cmd);
                        });
                    break;

                case StateEvent::LostConnection:
                    hb_manager_.Stop(drone_id);
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
    return GetContext(drone_id) != nullptr;
}

std::vector<DroneStatus> DroneManager::GetAllStatus() const
{
    std::vector<DroneStatus> result;
    result.reserve(drones_.size());
    for (const auto& [id, ctx] : drones_) {
        result.push_back(GetStatus(id));
    }
    return result;
}

DroneStatus DroneManager::GetStatus(int drone_id) const
{
    DroneStatus s;
    const auto* ctx = GetContext(drone_id);
    if (!ctx) return s;

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
    return s;
}

std::vector<DroneControlPacket> DroneManager::GetCommandQueueSnapshot(int drone_id) const
{
    const auto* ctx = GetContext(drone_id);
    if (!ctx || !ctx->command_queue) return {};
    return ctx->command_queue->Snapshot();
}

bool DroneManager::IsCommandQueuePaused(int drone_id) const
{
    const auto* ctx = GetContext(drone_id);
    return ctx && ctx->command_queue && ctx->command_queue->IsPaused();
}

size_t DroneManager::GetCommandQueueSize(int drone_id) const
{
    const auto* ctx = GetContext(drone_id);
    return (ctx && ctx->command_queue) ? ctx->command_queue->Size() : 0;
}

HeartbeatStats DroneManager::GetHeartbeatStats(int drone_id) const
{
    return hb_manager_.GetStats(drone_id);
}

void DroneManager::OnTelemetryReceived(int drone_id, const TelemetryData& data)
{
    auto* ctx = GetContext(drone_id);
    if (!ctx) {
        spdlog::warn("Telemetry for unknown drone {}", drone_id);
        return;
    }
    HandleTelemetry(*ctx, data);
}

void DroneManager::OnTelemetryReceivedBySlot(int slot, const TelemetryData& data)
{
    auto* ctx = GetContextBySlot(slot);
    if (!ctx) {
        spdlog::warn("Telemetry for unregistered slot {}", slot);
        return;
    }
    HandleTelemetry(*ctx, data);
}

int DroneManager::ResolveDroneIdBySlot(int slot) const
{
    const auto* ctx = GetContextBySlot(slot);
    return ctx ? ctx->drone_id : 0;
}

bool DroneManager::ProcessMoveCommand(int drone_id, double ue_x, double ue_y, double ue_z)
{
    auto* ctx = GetContext(drone_id);
    if (!ctx) return false;

    double ned_n, ned_e, ned_d;
    CoordinateConverter::UeOffsetToNed(ue_x, ue_y, ue_z, ned_n, ned_e, ned_d);

    ctx->last_ned_x = ned_n;
    ctx->last_ned_y = ned_e;
    ctx->last_ned_z = ned_d;
    hb_manager_.UpdateLastPosition(drone_id, ned_n, ned_e, ned_d);

    ctx->command_queue->Push(make_packet(ned_n, ned_e, ned_d, 1));
    spdlog::info("MoveCommand drone={}: NED({:.2f}, {:.2f}, {:.2f})",
                  drone_id, ned_n, ned_e, ned_d);
    return true;
}

bool DroneManager::ProcessPauseCommand(int drone_id)
{
    auto* ctx = GetContext(drone_id);
    if (!ctx) return false;

    ctx->command_queue->SetPaused(true);
    hb_manager_.UpdateLastPosition(drone_id, ctx->last_ned_x, ctx->last_ned_y, ctx->last_ned_z);
    spdlog::info("Pause drone {}", drone_id);
    return true;
}

bool DroneManager::ProcessResumeCommand(int drone_id)
{
    auto* ctx = GetContext(drone_id);
    if (!ctx) return false;

    ctx->command_queue->SetPaused(false);
    spdlog::info("Resume drone {}", drone_id);
    return true;
}

DroneConnectionState DroneManager::GetConnectionState(int drone_id) const
{
    const auto* ctx = GetContext(drone_id);
    return ctx ? ctx->state_machine->GetState() : DroneConnectionState::Offline;
}

TelemetryData DroneManager::GetLatestTelemetry(int drone_id) const
{
    const auto* ctx = GetContext(drone_id);
    return ctx ? ctx->latest_telemetry : TelemetryData{};
}

GpsAnchor DroneManager::GetAnchor(int drone_id) const
{
    return anchor_manager_.GetAnchor(drone_id);
}

void DroneManager::CheckTimeouts(int timeout_sec)
{
    for (auto& [id, ctx] : drones_) {
        ctx->state_machine->CheckTimeout(timeout_sec);
    }
}

DroneContext* DroneManager::GetContext(int drone_id)
{
    auto it = drones_.find(drone_id);
    return (it != drones_.end()) ? it->second.get() : nullptr;
}

const DroneContext* DroneManager::GetContext(int drone_id) const
{
    auto it = drones_.find(drone_id);
    return (it != drones_.end()) ? it->second.get() : nullptr;
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

void DroneManager::HandleTelemetry(DroneContext& ctx, const TelemetryData& data)
{
    const int drone_id = ctx.drone_id;

    ctx.latest_telemetry = data;
    ctx.has_telemetry = true;
    ctx.last_telemetry_unix = now_unix_seconds();

    if (data.gps_fix) {
        anchor_manager_.SetAnchor(drone_id, data.gps_lat, data.gps_lon, data.gps_alt);
    }

    ctx.state_machine->OnTelemetryReceived();
    hb_manager_.UpdateLastPosition(
        drone_id, data.position_ned[0], data.position_ned[1], data.position_ned[2]);

    if (data.battery >= 0 && data.battery > 20) {
        ctx.low_battery_alert_active = false;
    }
    if (data.battery >= 0 && data.battery <= 20 &&
        !ctx.low_battery_alert_active && alert_cb_) {
        ctx.low_battery_alert_active = true;
        alert_cb_(drone_id, "low_battery", data.battery);
    }

    if (telemetry_cb_) {
        telemetry_cb_(drone_id, data);
    }
}
