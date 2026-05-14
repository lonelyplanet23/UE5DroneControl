#include "state_machine.h"
#include <spdlog/spdlog.h>

namespace {
int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

StateMachine::StateMachine(OnStateChange on_change)
    : on_state_change_(std::move(on_change))
{
    last_telemetry_us_ = now_us();
}

DroneConnectionState StateMachine::GetState() const
{
    return state_;
}

void StateMachine::OnTelemetryReceived()
{
    last_telemetry_us_ = now_us();

    if (state_ == DroneConnectionState::Offline) {
        state_ = DroneConnectionState::Online;
        spdlog::info("[StateMachine] Offline → Online (PowerOn)");
        if (on_state_change_) {
            on_state_change_(StateEvent::PowerOn);
        }
    } else if (state_ == DroneConnectionState::Lost) {
        state_ = DroneConnectionState::Online;
        spdlog::info("[StateMachine] Lost → Online (Reconnect)");
        if (on_state_change_) {
            on_state_change_(StateEvent::Reconnect);
        }
    }
}

bool StateMachine::CheckTimeout(int timeout_sec)
{
    if (state_ != DroneConnectionState::Online) {
        return false;
    }

    int64_t elapsed_us = now_us() - last_telemetry_us_.load();
    int64_t elapsed_sec = elapsed_us / 1000000;

    if (elapsed_sec >= timeout_sec) {
        state_ = DroneConnectionState::Lost;
        spdlog::warn("[StateMachine] Online → Lost ({}s timeout)", elapsed_sec);
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
    last_telemetry_us_ = now_us();
    spdlog::info("[StateMachine] Reset → Offline");
}
