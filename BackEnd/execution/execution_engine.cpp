#include "execution/execution_engine.h"
#include "conversion/coordinate_converter.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

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

void ExecutionEngine::SetMoveCallback(MoveCallback cb)   { move_cb_ = std::move(cb); }
void ExecutionEngine::SetTelemetryGetter(TelemetryGetter cb) { telemetry_getter_ = std::move(cb); }
void ExecutionEngine::SetStateGetter(StateGetter cb)     { state_getter_ = std::move(cb); }

void ExecutionEngine::StartTasks(const AssemblyConfig& config)
{
    StopAll();

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (const auto& path : config.paths) {
            if (path.waypoints.empty()) continue;
            int drone_id = 0;
            const std::string& raw = path.drone_id;
            if (!raw.empty()) {
                std::string s = raw;
                if (s[0] == 'd' || s[0] == 'D') s = s.substr(1);
                try { drone_id = std::stoi(s); } catch (...) {}
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

    // 启动每机独立执行线程
    for (auto& [drone_id, task] : tasks_) {
        threads_[drone_id] = std::make_unique<std::thread>(
            &ExecutionEngine::RunDroneTask, this, drone_id);
    }

    // 启动避障线程
    avoidance_thread_ = std::thread([this]() {
        while (running_) {
            CheckAvoidance();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });

    spdlog::info("[Exec] Started {} drone tasks, mode={}", tasks_.size(), config.mode);
}

void ExecutionEngine::StopAll()
{
    running_ = false;

    if (avoidance_thread_.joinable()) avoidance_thread_.join();

    for (auto& [id, t] : threads_) {
        if (t && t->joinable()) t->join();
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
}

void ExecutionEngine::OnTelemetry(int drone_id, const TelemetryData& tel)
{
    // 遥测更新由 WaitForArrival 内部通过 telemetry_getter_ 轮询，无需额外处理
    (void)drone_id; (void)tel;
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
    wp.x = ue_x; wp.y = ue_y; wp.z = ue_z;
    it->second.override_target = wp;
    it->second.target_override = true;
    spdlog::info("[Exec] Target injected for drone {} ({:.0f},{:.0f},{:.0f})",
                 drone_id, ue_x, ue_y, ue_z);
}

// ============================================================
// 单机执行线程
// ============================================================
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

        // 巡逻模式：检查目标覆盖
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
                double ned_n, ned_e, ned_d;
                CoordinateConverter::UeOffsetToNed(
                    override_wp.x, override_wp.y, override_wp.z,
                    ned_n, ned_e, ned_d);
                spdlog::info("[Exec] Drone {} patrol: flying to target ({:.2f},{:.2f},{:.2f})",
                             drone_id, ned_n, ned_e, ned_d);
                {
                    std::lock_guard<std::mutex> lock(targets_mutex_);
                    targets_[drone_id] = {ned_n, ned_e, ned_d, false};
                }
                if (move_cb_) move_cb_(drone_id, ned_n, ned_e, ned_d);
                WaitForArrival(drone_id, ned_n, ned_e, ned_d);
                spdlog::info("[Exec] Drone {} patrol: target reached, stopping", drone_id);
                break;
            }
        }

        // 取当前航点
        int wp_idx = task.current_wp;
        if (wp_idx >= static_cast<int>(task.waypoints.size())) {
            if (task.mode == "recon" && task.closed_loop) {
                // 循环回到第一个航点
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                auto it = tasks_.find(drone_id);
                if (it != tasks_.end()) it->second.current_wp = 0;
                continue;
            }
            // 攻击模式或非循环侦察：到达最后航点后悬停
            spdlog::info("[Exec] Drone {} completed all waypoints, hovering", drone_id);
            break;
        }

        const auto& wp = task.waypoints[wp_idx];
        double ned_n, ned_e, ned_d;
        CoordinateConverter::UeOffsetToNed(wp.x, wp.y, wp.z, ned_n, ned_e, ned_d);

        spdlog::info("[Exec] Drone {} wp[{}]: NED({:.2f},{:.2f},{:.2f})",
                     drone_id, wp_idx, ned_n, ned_e, ned_d);

        {
            std::lock_guard<std::mutex> lock(targets_mutex_);
            targets_[drone_id] = {ned_n, ned_e, ned_d, false};
        }

        if (move_cb_) move_cb_(drone_id, ned_n, ned_e, ned_d);

        bool arrived = WaitForArrival(drone_id, ned_n, ned_e, ned_d);
        if (!arrived) break; // 被停止

        // 等待时间
        if (wp.wait_time > 0.0f) {
            auto wait_ms = static_cast<int>(wp.wait_time * 1000);
            for (int i = 0; i < wait_ms / 50 && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        // 推进到下一个航点
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            auto it = tasks_.find(drone_id);
            if (it != tasks_.end()) {
                it->second.current_wp++;
                // 侦察循环：到末尾时回绕
                if (it->second.mode == "recon" && it->second.closed_loop &&
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
        // 检查是否被巡逻目标覆盖
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            auto it = tasks_.find(drone_id);
            if (it != tasks_.end() && it->second.mode == "patrol" &&
                it->second.target_override) {
                return true; // 让外层循环处理覆盖
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

// ============================================================
// 实时避障（基础版）
// ============================================================
void ExecutionEngine::CheckAvoidance()
{
    if (!move_cb_ || !telemetry_getter_) return;

    std::vector<std::pair<int, DroneTarget>> snapshot;
    {
        std::lock_guard<std::mutex> lock(targets_mutex_);
        for (const auto& [id, tgt] : targets_) {
            snapshot.push_back({id, tgt});
        }
    }

    if (snapshot.size() < 2) return;

    // 获取所有在线无人机的当前位置和速度
    struct DroneState {
        int id;
        double pos[3];
        double vel[3];
    };
    std::vector<DroneState> states;
    for (const auto& [id, tgt] : snapshot) {
        if (!telemetry_getter_) continue;
        auto tel = telemetry_getter_(id);
        states.push_back({id,
            {tel.position_ned[0], tel.position_ned[1], tel.position_ned[2]},
            {tel.velocity[0], tel.velocity[1], tel.velocity[2]}});
    }

    // 预测碰撞：检查每对无人机
    for (size_t i = 0; i < states.size(); ++i) {
        for (size_t j = i + 1; j < states.size(); ++j) {
            const auto& a = states[i];
            const auto& b = states[j];

            // 当前距离
            double dx = a.pos[0] - b.pos[0];
            double dy = a.pos[1] - b.pos[1];
            double dz = a.pos[2] - b.pos[2];
            double dist_now = std::sqrt(dx * dx + dy * dy + dz * dz);

            // 预测 lookahead 秒后的位置
            double ax_pred = a.pos[0] + a.vel[0] * avoidance_lookahead_sec_;
            double ay_pred = a.pos[1] + a.vel[1] * avoidance_lookahead_sec_;
            double az_pred = a.pos[2] + a.vel[2] * avoidance_lookahead_sec_;
            double bx_pred = b.pos[0] + b.vel[0] * avoidance_lookahead_sec_;
            double by_pred = b.pos[1] + b.vel[1] * avoidance_lookahead_sec_;
            double bz_pred = b.pos[2] + b.vel[2] * avoidance_lookahead_sec_;

            double pdx = ax_pred - bx_pred;
            double pdy = ay_pred - by_pred;
            double pdz = az_pred - bz_pred;
            double dist_pred = std::sqrt(pdx * pdx + pdy * pdy + pdz * pdz);

            if (dist_pred < avoidance_radius_m_ || dist_now < avoidance_radius_m_) {
                // 对 ID 较大的无人机（优先级低）施加侧向偏移
                int low_prio_id = (a.id > b.id) ? a.id : b.id;
                const auto& low_state = (a.id > b.id) ? a : b;

                // 找到该机的原始目标
                DroneTarget orig_tgt;
                {
                    std::lock_guard<std::mutex> lock(targets_mutex_);
                    auto it = targets_.find(low_prio_id);
                    if (it == targets_.end() || it->second.avoidance_active) continue;
                    orig_tgt = it->second;
                    it->second.avoidance_active = true;
                }

                // 侧向偏移：在 NE 平面垂直于当前速度方向偏移 avoidance_radius_m_
                double offset = avoidance_radius_m_ * 1.5;
                double adj_n = orig_tgt.ned_n + offset;
                double adj_e = orig_tgt.ned_e + offset;
                double adj_d = orig_tgt.ned_d;

                spdlog::warn("[Avoidance] Drone {} too close to drone {}, dist={:.2f}m, adjusting",
                             low_prio_id, (a.id > b.id) ? b.id : a.id, dist_now);

                if (move_cb_) move_cb_(low_prio_id, adj_n, adj_e, adj_d);

                // 短暂等待后恢复原目标（在后台线程中）
                std::thread([this, low_prio_id, orig_tgt]() {
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    if (!running_) return;
                    {
                        std::lock_guard<std::mutex> lock(targets_mutex_);
                        auto it = targets_.find(low_prio_id);
                        if (it != targets_.end()) it->second.avoidance_active = false;
                    }
                    if (move_cb_) move_cb_(low_prio_id, orig_tgt.ned_n, orig_tgt.ned_e, orig_tgt.ned_d);
                    spdlog::info("[Avoidance] Drone {} restored to original target", low_prio_id);
                }).detach();
            }
        }
    }
}
