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

AssemblyController::AssemblyController(int timeout_sec, double arrival_threshold_m)
    : timeout_sec_(timeout_sec)
    , arrival_threshold_m_(arrival_threshold_m)
{
}

bool AssemblyController::Start(const AssemblyConfig& config, double safety_cylinder_m)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != AssemblyState::Idle) {
        spdlog::warn("[Assembly] Already in state {}", static_cast<int>(state_));
        return false;
    }

    config_ = config;
    arrivals_.clear();

    // ---- Step 1: 解析路径，收集无人机和目标点 ----
    std::vector<AssemblyTarget> targets;
    std::unordered_map<int, std::tuple<double, double, double>> drone_positions;

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

        targets.push_back({drone_id, ned_n, ned_e, ned_d,
                           static_cast<int>(targets.size())});

        // 获取无人机当前位置
        if (position_getter_) {
            auto tel = position_getter_(drone_id);
            drone_positions[drone_id] = std::make_tuple(
                tel.position_ned[0], tel.position_ned[1], tel.position_ned[2]);
        }
    }

    if (targets.empty()) {
        spdlog::warn("[Assembly] No valid paths, aborting");
        return false;
    }

    // ---- Step 2: 运行规划器（P1 最优分配 + P2 高度分离） ----
    if (!drone_positions.empty() && safety_cylinder_m > 0) {
        auto conflicts = planner_.Plan(drone_positions, targets, safety_cylinder_m);
        spdlog::info("[Assembly] Planner: {} conflicts detected, {} drones planned",
                     conflicts.size(), targets.size());
    }

    // ---- Step 3: 按规划结果构建 arrivals_ 并发送指令 ----
    for (const auto& t : targets) {
        arrivals_.push_back(DroneArrival{
            t.drone_id, t.ned_x, t.ned_y, t.ned_z, false
        });
        spdlog::info("[Assembly] Drone {} target: NED({:.2f}, {:.2f}, {:.2f})",
                     t.drone_id, t.ned_x, t.ned_y, t.ned_z);

        if (move_cmd_cb_) {
            move_cmd_cb_(t.drone_id, t.ned_x, t.ned_y, t.ned_z);
        }
    }

    state_ = AssemblyState::Assembling;
    start_time_ = std::chrono::steady_clock::now();

    spdlog::info("[Assembly] Started '{}' with {} drones, mode={}",
                 config_.array_id, arrivals_.size(), config_.mode);

    if (progress_cb_) {
        progress_cb_(GetProgressUnsafe());
    }

    return true;
}

void AssemblyController::UpdateDronePosition(int drone_id, double ned_x, double ned_y, double ned_z)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != AssemblyState::Assembling) return;

    bool any_changed = false;

    for (auto& arrival : arrivals_) {
        if (arrival.drone_id != drone_id || arrival.arrived) continue;

        const double dx = ned_x - arrival.target_ned_x;
        const double dy = ned_y - arrival.target_ned_y;
        const double dz = ned_z - arrival.target_ned_z;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        spdlog::debug("[Assembly] Drone {} position check: dist={:.2f}m (threshold={:.2f}m)",
                      drone_id, dist, arrival_threshold_m_);

        if (dist < arrival_threshold_m_) {
            arrival.arrived = true;
            any_changed = true;
            spdlog::info("[Assembly] Drone {} arrived at slot (dist={:.2f}m)",
                         drone_id, dist);
        }
    }

    if (!any_changed) return;

    auto progress = GetProgressUnsafe();
    if (progress_cb_) {
        progress_cb_(progress);
    }

    if (progress.ready_count >= progress.total_count) {
        state_ = AssemblyState::Executing;
        spdlog::info("[Assembly] All drones ready, entering executing state");
    }
}

bool AssemblyController::CheckTimeout()
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != AssemblyState::Assembling) return false;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time_).count();

    if (elapsed >= timeout_sec_) {
        state_ = AssemblyState::Timeout;
        auto progress = GetProgressUnsafe();
        spdlog::warn("[Assembly] Timeout after {}s! {}/{} arrived",
                     elapsed, progress.ready_count, progress.total_count);
        if (timeout_cb_) {
            timeout_cb_(progress);
        }
        return true;
    }

    return false;
}

void AssemblyController::Stop()
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = AssemblyState::Idle;
    arrivals_.clear();
    spdlog::info("[Assembly] Stopped");
}

AssemblyProgress AssemblyController::GetProgress() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return GetProgressUnsafe();
}

AssemblyProgress AssemblyController::GetProgressUnsafe() const
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

void AssemblyController::SetTimeoutCallback(TimeoutCallback cb)   { timeout_cb_  = std::move(cb); }
void AssemblyController::SetMoveCommandCallback(MoveCommandCallback cb) { move_cmd_cb_ = std::move(cb); }
void AssemblyController::SetPositionGetter(DronePositionGetter cb)    { position_getter_ = std::move(cb); }
