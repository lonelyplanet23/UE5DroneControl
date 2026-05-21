#include "execution/execution_engine.h"
#include "conversion/coordinate_converter.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3 make_vec3(double x, double y, double z)
{
    return {x, y, z};
}

Vec3 operator+(const Vec3& a, const Vec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(const Vec3& v, double scale)
{
    return {v.x * scale, v.y * scale, v.z * scale};
}

double dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double length_sq(const Vec3& v)
{
    return dot(v, v);
}

double length(const Vec3& v)
{
    return std::sqrt(length_sq(v));
}

Vec3 normalize_xy(const Vec3& v)
{
    const Vec3 flat{v.x, v.y, 0.0};
    const double len = length(flat);
    if (len < 1e-9) {
        return {1.0, 0.0, 0.0};
    }
    return flat * (1.0 / len);
}

double segment_segment_distance_sq(const Vec3& p0, const Vec3& p1,
                                   const Vec3& q0, const Vec3& q1)
{
    const Vec3 u = p1 - p0;
    const Vec3 v = q1 - q0;
    const Vec3 w = p0 - q0;
    const double a = dot(u, u);
    const double b = dot(u, v);
    const double c = dot(v, v);
    const double d = dot(u, w);
    const double e = dot(v, w);
    const double D = a * c - b * b;
    const double EPS = 1e-12;

    double sN = 0.0;
    double sD = D;
    double tN = 0.0;
    double tD = D;

    if (D < EPS) {
        sN = 0.0;
        sD = 1.0;
        tN = e;
        tD = c;
    } else {
        sN = b * e - c * d;
        tN = a * e - b * d;

        if (sN < 0.0) {
            sN = 0.0;
            tN = e;
            tD = c;
        } else if (sN > sD) {
            sN = sD;
            tN = e + b;
            tD = c;
        }
    }

    if (tN < 0.0) {
        tN = 0.0;
        if (-d < 0.0) {
            sN = 0.0;
        } else if (-d > a) {
            sN = sD;
        } else {
            sN = -d;
            sD = a;
        }
    } else if (tN > tD) {
        tN = tD;
        if ((-d + b) < 0.0) {
            sN = 0.0;
        } else if ((-d + b) > a) {
            sN = sD;
        } else {
            sN = -d + b;
            sD = a;
        }
    }

    const double sc = (std::fabs(sN) < EPS ? 0.0 : sN / sD);
    const double tc = (std::fabs(tN) < EPS ? 0.0 : tN / tD);
    const Vec3 dp = w + (u * sc) - (v * tc);
    return length_sq(dp);
}

} // namespace

ExecutionEngine::ExecutionEngine(double arrival_threshold_m,
                                 double avoidance_radius_m,
                                 double avoidance_lookahead_sec)
    : arrival_threshold_m_(arrival_threshold_m)
    , avoidance_radius_m_(avoidance_radius_m)
    , avoidance_lookahead_sec_(avoidance_lookahead_sec)
{
}

ExecutionEngine::~ExecutionEngine()
{
    StopAll();
}

void ExecutionEngine::SetMoveCallback(MoveCallback cb) { move_cb_ = std::move(cb); }
void ExecutionEngine::SetTelemetryGetter(TelemetryGetter cb) { telemetry_getter_ = std::move(cb); }
void ExecutionEngine::SetStateGetter(StateGetter cb) { state_getter_ = std::move(cb); }
void ExecutionEngine::SetAvoidanceCallback(AvoidanceCallback cb) { avoidance_cb_ = std::move(cb); }

void ExecutionEngine::StartTasks(const AssemblyConfig& config)
{
    StopAll();

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (const auto& path : config.paths) {
            if (path.waypoints.empty()) continue;

            int drone_id = 0;
            if (!path.drone_id.empty()) {
                std::string s = path.drone_id;
                if (s[0] == 'd' || s[0] == 'D') s = s.substr(1);
                try {
                    drone_id = std::stoi(s);
                } catch (...) {
                    drone_id = 0;
                }
            }

            if (drone_id <= 0) {
                spdlog::warn("[Exec] Invalid drone_id: {}", path.drone_id);
                continue;
            }

            DroneTask task;
            task.drone_id = drone_id;
            task.mode = config.mode;
            task.closed_loop = path.closed_loop;
            task.waypoints = path.waypoints;
            task.current_wp = 0;
            tasks_[drone_id] = std::move(task);
        }
    }

    if (tasks_.empty()) {
        spdlog::warn("[Exec] No valid tasks to start");
        return;
    }

    running_ = true;

    for (const auto& [drone_id, task] : tasks_) {
        (void)task;
        threads_[drone_id] = std::make_unique<std::thread>(&ExecutionEngine::RunDroneTask, this, drone_id);
    }

    avoidance_thread_ = std::thread(&ExecutionEngine::CheckAvoidanceLoop, this);

    spdlog::info("[Exec] Started {} drone tasks, mode={}", tasks_.size(), config.mode);
}

void ExecutionEngine::StopAll()
{
    running_ = false;

    if (avoidance_thread_.joinable()) {
        avoidance_thread_.join();
    }

    for (auto& [id, thread_ptr] : threads_) {
        (void)id;
        if (thread_ptr && thread_ptr->joinable()) {
            thread_ptr->join();
        }
    }
    threads_.clear();

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(targets_mutex_);
        targets_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(avoidance_stats_mutex_);
        avoidance_stats_ = AvoidanceStats{};
    }
}

void ExecutionEngine::OnTelemetry(int drone_id, const TelemetryData& tel)
{
    (void)drone_id;
    (void)tel;
}

void ExecutionEngine::InjectTarget(int drone_id, double ue_x, double ue_y, double ue_z)
{
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto it = tasks_.find(drone_id);
    if (it == tasks_.end()) return;

    if (it->second.mode != "patrol") {
        spdlog::warn("[Exec] InjectTarget: drone {} is not in patrol mode", drone_id);
        return;
    }

    AssemblyConfig::Path::Waypoint wp;
    wp.x = ue_x;
    wp.y = ue_y;
    wp.z = ue_z;
    it->second.override_target = wp;
    it->second.target_override = true;
    spdlog::info("[Exec] Target injected for drone {} ({:.0f},{:.0f},{:.0f})",
                 drone_id, ue_x, ue_y, ue_z);
}

std::vector<ExecutionTaskSnapshot> ExecutionEngine::GetTaskSnapshots() const
{
    std::vector<ExecutionTaskSnapshot> snapshots;
    std::scoped_lock lock(tasks_mutex_, targets_mutex_);
    snapshots.reserve(tasks_.size());

    for (const auto& [drone_id, task] : tasks_) {
        ExecutionTaskSnapshot snapshot;
        snapshot.drone_id = drone_id;
        snapshot.mode = task.mode;
        snapshot.closed_loop = task.closed_loop;
        snapshot.current_wp = task.current_wp;
        snapshot.waypoint_count = static_cast<int>(task.waypoints.size());
        snapshot.target_override = task.target_override;

        auto target_it = targets_.find(drone_id);
        if (target_it != targets_.end()) {
            snapshot.avoidance_active = target_it->second.avoidance_active;
            snapshot.base_ned_n = target_it->second.base_ned_n;
            snapshot.base_ned_e = target_it->second.base_ned_e;
            snapshot.base_ned_d = target_it->second.base_ned_d;
            snapshot.target_ned_n = target_it->second.ned_n;
            snapshot.target_ned_e = target_it->second.ned_e;
            snapshot.target_ned_d = target_it->second.ned_d;
        }

        snapshots.push_back(snapshot);
    }

    return snapshots;
}

AvoidanceStats ExecutionEngine::GetAvoidanceStats() const
{
    std::lock_guard<std::mutex> lock(avoidance_stats_mutex_);
    return avoidance_stats_;
}

void ExecutionEngine::RunDroneTask(int drone_id)
{
    spdlog::info("[Exec] Drone {} task thread started", drone_id);

    while (running_) {
        DroneTask task;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            auto it = tasks_.find(drone_id);
            if (it == tasks_.end()) break;
            task = it->second;
        }

        if (task.waypoints.empty()) break;

        if (task.mode == "patrol") {
            bool has_override = false;
            AssemblyConfig::Path::Waypoint override_wp;
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                auto it = tasks_.find(drone_id);
                if (it != tasks_.end() && it->second.target_override) {
                    has_override = true;
                    override_wp = it->second.override_target;
                    it->second.target_override = false;
                }
            }

            if (has_override) {
                double ned_n = 0.0, ned_e = 0.0, ned_d = 0.0;
                CoordinateConverter::UeOffsetToNed(
                    override_wp.x, override_wp.y, override_wp.z,
                    ned_n, ned_e, ned_d);

                spdlog::info("[Exec] Drone {} patrol: flying to target ({:.2f},{:.2f},{:.2f})",
                             drone_id, ned_n, ned_e, ned_d);
                {
                    std::lock_guard<std::mutex> lock(targets_mutex_);
                    auto& target = targets_[drone_id];
                    target.base_ned_n = ned_n;
                    target.base_ned_e = ned_e;
                    target.base_ned_d = ned_d;
                    target.ned_n = ned_n;
                    target.ned_e = ned_e;
                    target.ned_d = ned_d;
                    target.avoidance_active = false;
                    target.avoidance_restore_at = {};
                    target.avoidance_cooldown_until = {};
                }
                if (move_cb_) move_cb_(drone_id, ned_n, ned_e, ned_d);
                WaitForArrival(drone_id, ned_n, ned_e, ned_d);
                spdlog::info("[Exec] Drone {} patrol: target reached, stopping", drone_id);
                break;
            }
        }

        int wp_idx = task.current_wp;
        if (wp_idx >= static_cast<int>(task.waypoints.size())) {
            if (task.mode == "scout" && task.closed_loop) {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                auto it = tasks_.find(drone_id);
                if (it != tasks_.end()) {
                    it->second.current_wp = 0;
                }
                continue;
            }

            spdlog::info("[Exec] Drone {} completed all waypoints, hovering", drone_id);
            break;
        }

        const auto& wp = task.waypoints[wp_idx];
        double ned_n = 0.0, ned_e = 0.0, ned_d = 0.0;
        CoordinateConverter::UeOffsetToNed(wp.x, wp.y, wp.z, ned_n, ned_e, ned_d);

        spdlog::info("[Exec] Drone {} wp[{}]: NED({:.2f},{:.2f},{:.2f})",
                     drone_id, wp_idx, ned_n, ned_e, ned_d);

        {
            std::lock_guard<std::mutex> lock(targets_mutex_);
            auto& target = targets_[drone_id];
            target.base_ned_n = ned_n;
            target.base_ned_e = ned_e;
            target.base_ned_d = ned_d;
            target.ned_n = ned_n;
            target.ned_e = ned_e;
            target.ned_d = ned_d;
            target.avoidance_active = false;
            target.avoidance_restore_at = {};
            target.avoidance_cooldown_until = {};
        }

        if (move_cb_) move_cb_(drone_id, ned_n, ned_e, ned_d);

        bool arrived = WaitForArrival(drone_id, ned_n, ned_e, ned_d);
        if (!arrived) break;

        if (wp.wait_time > 0.0f) {
            const auto wait_ms = static_cast<int>(wp.wait_time * 1000);
            for (int i = 0; i < wait_ms / 50 && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            auto it = tasks_.find(drone_id);
            if (it != tasks_.end()) {
                it->second.current_wp++;
                if (it->second.mode == "scout" && it->second.closed_loop &&
                    it->second.current_wp >= static_cast<int>(it->second.waypoints.size())) {
                    it->second.current_wp = 0;
                }
            }
        }
    }

    spdlog::info("[Exec] Drone {} task thread finished", drone_id);
}

bool ExecutionEngine::WaitForArrival(int drone_id, double ned_n, double ned_e, double ned_d)
{
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            auto it = tasks_.find(drone_id);
            if (it != tasks_.end() && it->second.mode == "patrol" && it->second.target_override) {
                return true;
            }
        }

        if (!telemetry_getter_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        auto tel = telemetry_getter_(drone_id);
        const double dx = tel.position_ned[0] - ned_n;
        const double dy = tel.position_ned[1] - ned_e;
        const double dz = tel.position_ned[2] - ned_d;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist < arrival_threshold_m_) {
            spdlog::info("[Exec] Drone {} arrived (dist={:.2f}m)", drone_id, dist);
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return false;
}

void ExecutionEngine::CheckAvoidance()
{
    if (!move_cb_ || !telemetry_getter_) return;

    const auto now = std::chrono::steady_clock::now();
    RestoreExpiredAvoidanceTargets(now);

    std::vector<std::pair<int, DroneTarget>> snapshot;
    {
        std::lock_guard<std::mutex> lock(targets_mutex_);
        snapshot.reserve(targets_.size());
        for (const auto& [id, tgt] : targets_) {
            snapshot.push_back({id, tgt});
        }
    }

    if (snapshot.size() < 2) return;

    struct DroneState {
        int id;
        Vec3 pos;
        Vec3 vel;
    };

    std::vector<DroneState> states;
    states.reserve(snapshot.size());
    for (const auto& [id, tgt] : snapshot) {
        (void)tgt;
        if (state_getter_) {
            const auto state = state_getter_(id);
            if (state != DroneConnectionState::Online && state != DroneConnectionState::Connecting) {
                continue;
            }
        }

        auto tel = telemetry_getter_(id);
        states.push_back({id,
            {tel.position_ned[0], tel.position_ned[1], tel.position_ned[2]},
            {tel.velocity[0], tel.velocity[1], tel.velocity[2]}});
    }

    for (size_t i = 0; i < states.size(); ++i) {
        for (size_t j = i + 1; j < states.size(); ++j) {
            const auto& a = states[i];
            const auto& b = states[j];

            const double dist_now = length(a.pos - b.pos);
            const Vec3 a_pred = a.pos + a.vel * avoidance_lookahead_sec_;
            const Vec3 b_pred = b.pos + b.vel * avoidance_lookahead_sec_;
            const double dist_pred = length(a_pred - b_pred);
            const double segment_dist_sq = segment_segment_distance_sq(a.pos, a_pred, b.pos, b_pred);

            if (dist_now >= avoidance_radius_m_ &&
                dist_pred >= avoidance_radius_m_ &&
                segment_dist_sq >= avoidance_radius_m_ * avoidance_radius_m_) {
                continue;
            }

            const int low_prio_id = (a.id > b.id) ? a.id : b.id;
            const int other_id = (a.id > b.id) ? b.id : a.id;
            const auto& low_state = (a.id > b.id) ? a : b;
            const auto& other_state = (a.id > b.id) ? b : a;

            DroneTarget original;
            {
                std::lock_guard<std::mutex> lock(targets_mutex_);
                auto it = targets_.find(low_prio_id);
                if (it == targets_.end()) continue;
                if (it->second.avoidance_active) continue;
                if (now < it->second.avoidance_cooldown_until) continue;

                original = it->second;
                it->second.base_ned_n = it->second.ned_n;
                it->second.base_ned_e = it->second.ned_e;
                it->second.base_ned_d = it->second.ned_d;
                it->second.avoidance_active = true;
                it->second.avoidance_restore_at = now + std::chrono::seconds(3);
                it->second.avoidance_cooldown_until = now + std::chrono::seconds(6);
            }

            const Vec3 travel_dir = normalize_xy(other_state.pos - low_state.pos);
            const Vec3 lateral_dir{-travel_dir.y, travel_dir.x, 0.0};
            const double offset_distance = std::max(avoidance_radius_m_ * 1.5, 1.0);
            const double vertical_offset = std::clamp(
                (other_state.pos.z - low_state.pos.z) * 0.25,
                -1.5, 1.5);
            const Vec3 offset = lateral_dir * offset_distance + Vec3{0.0, 0.0, vertical_offset};
            const Vec3 base = make_vec3(original.base_ned_n, original.base_ned_e, original.base_ned_d);
            const Vec3 applied = base + offset;

            {
                std::lock_guard<std::mutex> lock(targets_mutex_);
                auto it = targets_.find(low_prio_id);
                if (it != targets_.end()) {
                    it->second.ned_n = applied.x;
                    it->second.ned_e = applied.y;
                    it->second.ned_d = applied.z;
                }
            }

            AvoidanceEvent event;
            event.drone_id = low_prio_id;
            event.other_drone_id = other_id;
            event.current_distance_m = dist_now;
            event.predicted_distance_m = dist_pred;
            event.threshold_m = avoidance_radius_m_;
            event.base_ned_n = original.base_ned_n;
            event.base_ned_e = original.base_ned_e;
            event.base_ned_d = original.base_ned_d;
            event.applied_ned_n = applied.x;
            event.applied_ned_e = applied.y;
            event.applied_ned_d = applied.z;
            event.offset_n = offset.x;
            event.offset_e = offset.y;
            event.offset_d = offset.z;
            event.activated = true;
            event.restored = false;
            event.valid = true;

            {
                std::lock_guard<std::mutex> lock(avoidance_stats_mutex_);
                avoidance_stats_.events_total++;
                avoidance_stats_.activations_total++;
                avoidance_stats_.active_count++;
                avoidance_stats_.has_last_event = true;
                avoidance_stats_.last_event = event;
            }

            spdlog::warn("[Avoidance] Drone {} too close to drone {}, dist={:.2f}m pred={:.2f}m",
                         low_prio_id, other_id, dist_now, dist_pred);

            if (avoidance_cb_) {
                avoidance_cb_(event);
            }
            if (move_cb_) {
                move_cb_(low_prio_id, applied.x, applied.y, applied.z);
            }
        }
    }
}

void ExecutionEngine::CheckAvoidanceLoop()
{
    while (running_) {
        CheckAvoidance();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void ExecutionEngine::RestoreExpiredAvoidanceTargets(const std::chrono::steady_clock::time_point& now)
{
    struct RestoreItem {
        int drone_id = 0;
        double ned_n = 0.0;
        double ned_e = 0.0;
        double ned_d = 0.0;
    };

    std::vector<RestoreItem> items;
    {
        std::lock_guard<std::mutex> lock(targets_mutex_);
        for (auto& [drone_id, target] : targets_) {
            if (!target.avoidance_active) continue;
            if (now < target.avoidance_restore_at) continue;

            target.avoidance_active = false;
            target.avoidance_restore_at = {};
            target.ned_n = target.base_ned_n;
            target.ned_e = target.base_ned_e;
            target.ned_d = target.base_ned_d;
            items.push_back({drone_id, target.ned_n, target.ned_e, target.ned_d});
        }
    }

    for (const auto& item : items) {
        {
            std::lock_guard<std::mutex> lock(avoidance_stats_mutex_);
            avoidance_stats_.events_total++;
            avoidance_stats_.restorations_total++;
            if (avoidance_stats_.active_count > 0) {
                avoidance_stats_.active_count--;
            }
            avoidance_stats_.has_last_event = true;
            avoidance_stats_.last_event.drone_id = item.drone_id;
            avoidance_stats_.last_event.base_ned_n = item.ned_n;
            avoidance_stats_.last_event.base_ned_e = item.ned_e;
            avoidance_stats_.last_event.base_ned_d = item.ned_d;
            avoidance_stats_.last_event.applied_ned_n = item.ned_n;
            avoidance_stats_.last_event.applied_ned_e = item.ned_e;
            avoidance_stats_.last_event.applied_ned_d = item.ned_d;
            avoidance_stats_.last_event.activated = false;
            avoidance_stats_.last_event.restored = true;
            avoidance_stats_.last_event.valid = true;
        }

        if (avoidance_cb_) {
            AvoidanceEvent event;
            event.drone_id = item.drone_id;
            event.base_ned_n = item.ned_n;
            event.base_ned_e = item.ned_e;
            event.base_ned_d = item.ned_d;
            event.applied_ned_n = item.ned_n;
            event.applied_ned_e = item.ned_e;
            event.applied_ned_d = item.ned_d;
            event.restored = true;
            event.valid = true;
            avoidance_cb_(event);
        }

        if (move_cb_) {
            move_cb_(item.drone_id, item.ned_n, item.ned_e, item.ned_d);
        }

        spdlog::info("[Avoidance] Drone {} restored to original target", item.drone_id);
    }
}
