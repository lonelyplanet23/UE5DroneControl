#include "state_machine.h"
#include <spdlog/spdlog.h>

StateMachine::StateMachine(OnStateChange on_change)
    : on_state_change_(std::move(on_change))
{
    last_telemetry_time_ = std::chrono::steady_clock::now();
}

DroneConnectionState StateMachine::GetState() const
{
    return state_;
}

void StateMachine::OnTelemetryReceived()
{
    last_telemetry_time_ = std::chrono::steady_clock::now();

    if (state_ == DroneConnectionState::Offline) {
        // Offline → Online: PowerOn
        state_ = DroneConnectionState::Online;
        spdlog::info("[StateMachine] Offline → Online (PowerOn)");
        if (on_state_change_) {
            on_state_change_(StateEvent::PowerOn);
        }
    } else if (state_ == DroneConnectionState::Lost) {
        // Lost → Online: Reconnect
        state_ = DroneConnectionState::Online;
        spdlog::info("[StateMachine] Lost → Online (Reconnect)");
        if (on_state_change_) {
            on_state_change_(StateEvent::Reconnect);
        }
    }
    // Online → Online: 无事件，只更新时间戳
}

bool StateMachine::CheckTimeout(int timeout_sec)
{
    if (state_ != DroneConnectionState::Online) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_telemetry_time_).count();

    if (elapsed >= timeout_sec) {
        state_ = DroneConnectionState::Lost;
        spdlog::warn("[StateMachine] Online → Lost ({}s timeout)", elapsed);
        if (on_state_change_) {
            on_state_change_(StateEvent::LostConnection);
        }
        return true;
    }

    return false;
}

void StateMachine::Reset()
{
    state_ = DroneConnectionState::Offline;
    last_telemetry_time_ = std::chrono::steady_clock::now();
    spdlog::info("[StateMachine] Reset → Offline");
}
