#include "assembly_controller.h"

#include "conversion/coordinate_converter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <spdlog/spdlog.h>

namespace {

int parse_drone_id(const std::string& raw)
{
    if (raw.empty()) return 0;
    std::string text = raw;
    if (text[0] == 'd' || text[0] == 'D') {
        text = text.substr(1);
    }
    if (text.empty()) return 0;
    return std::all_of(text.begin(), text.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    }) ? std::stoi(text) : 0;
}

} // namespace

AssemblyController::AssemblyController(int timeout_sec)
    : timeout_sec_(timeout_sec)
{
}

bool AssemblyController::Start(const AssemblyConfig& config)
{
    if (state_ != AssemblyState::Idle) {
        spdlog::warn("[Assembly] Already in state {}", static_cast<int>(state_));
        return false;
    }

    config_ = config;
    arrivals_.clear();

    for (const auto& path : config.paths) {
        if (path.waypoints.empty()) continue;

        const auto& wp = path.waypoints[0];
        double ned_n, ned_e, ned_d;
        CoordinateConverter::UeOffsetToNed(wp.x, wp.y, wp.z, ned_n, ned_e, ned_d);

        int drone_id = parse_drone_id(path.drone_id);
        if (drone_id <= 0) {
            spdlog::warn("[Assembly] Invalid drone_id: {}", path.drone_id);
            continue;
        }

        arrivals_.push_back(DroneArrival{
            drone_id, ned_n, ned_e, ned_d, false
        });
        spdlog::info("[Assembly] Drone {} target: NED({:.2f}, {:.2f}, {:.2f})",
                     drone_id, ned_n, ned_e, ned_d);
    }

    if (arrivals_.empty()) {
        spdlog::warn("[Assembly] No valid paths, aborting");
        return false;
    }

    state_ = AssemblyState::Assembling;
    start_time_ = std::chrono::steady_clock::now();

    spdlog::info("[Assembly] Started '{}' with {} drones, mode={}",
                 config.array_id, arrivals_.size(), config.mode);

    if (progress_cb_) {
        progress_cb_(GetProgress());
    }

    return true;
}

void AssemblyController::UpdateDronePosition(int drone_id, double ned_x, double ned_y, double ned_z)
{
    if (state_ != AssemblyState::Assembling) return;

    bool any_changed = false;

    for (auto& arrival : arrivals_) {
        if (arrival.drone_id != drone_id || arrival.arrived) continue;

        const double dx = ned_x - arrival.target_ned_x;
        const double dy = ned_y - arrival.target_ned_y;
        const double dz = ned_z - arrival.target_ned_z;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        constexpr double threshold = 1.0;
        if (dist < threshold) {
            arrival.arrived = true;
            any_changed = true;
            spdlog::info("[Assembly] Drone {} arrived at slot (dist={:.2f}m)",
                         drone_id, dist);
        }
    }

    if (!any_changed) return;

    auto progress = GetProgress();
    if (progress_cb_) {
        progress_cb_(progress);
    }

    if (progress.ready_count >= progress.total_count) {
        state_ = AssemblyState::Ready;
        spdlog::info("[Assembly] All drones ready");
    }
}

bool AssemblyController::CheckTimeout()
{
    if (state_ != AssemblyState::Assembling) return false;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time_).count();

    if (elapsed >= timeout_sec_) {
        state_ = AssemblyState::Timeout;
        spdlog::warn("[Assembly] Timeout after {}s! {}/{} arrived",
                     elapsed, GetProgress().ready_count, GetProgress().total_count);
        return true;
    }

    return false;
}

void AssemblyController::Stop()
{
    state_ = AssemblyState::Idle;
    arrivals_.clear();
    spdlog::info("[Assembly] Stopped");
}

AssemblyProgress AssemblyController::GetProgress() const
{
    AssemblyProgress progress;
    progress.array_id = config_.array_id;
    progress.total_count = static_cast<int>(arrivals_.size());
    for (const auto& a : arrivals_) {
        if (a.arrived) progress.ready_count++;
    }
    return progress;
}

void AssemblyController::SetProgressCallback(ProgressCallback cb)
{
    progress_cb_ = std::move(cb);
}
