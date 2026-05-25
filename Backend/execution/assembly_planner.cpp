#include "assembly_planner.h"
#include "conversion/assignment_solver.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

// ============================================================
// P1: 最优分配
// ============================================================
std::vector<int> AssemblyPlanner::AssignOptimal(
    const std::vector<AssemblyTarget>& targets,
    const std::unordered_map<int, std::tuple<double, double, double>>& positions) const
{
    const int n = static_cast<int>(targets.size());
    if (n == 0) return {};

    // 收集无人机列表
    std::vector<int> drone_ids;
    drone_ids.reserve(positions.size());
    for (const auto& [id, _] : positions) {
        drone_ids.push_back(id);
    }

    const int m = static_cast<int>(drone_ids.size());
    if (m == 0) return {};

    // 构建 M×N 代价矩阵: cost[i][j] = distance(drone_i, target_j)
    std::vector<std::vector<double>> cost(m, std::vector<double>(n));
    for (int i = 0; i < m; ++i) {
        const auto& [dx, dy, dz] = positions.at(drone_ids[i]);
        for (int j = 0; j < n; ++j) {
            double ddx = dx - targets[j].ned_x;
            double ddy = dy - targets[j].ned_y;
            double ddz = dz - targets[j].ned_z;
            cost[i][j] = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
        }
    }

    // 匈牙利求解
    auto assignment = AssignmentSolver::HungarianMinCost(cost);

    // assignment[i] = 分配给 drone i 的目标索引 j
    std::vector<int> result(m, -1);
    for (int i = 0; i < m && i < static_cast<int>(assignment.size()); ++i) {
        result[i] = assignment[i];
    }

    // 日志
    double total = 0;
    for (int i = 0; i < m; ++i) {
        if (result[i] >= 0) {
            total += cost[i][result[i]];
            spdlog::info("[Planner] Drone {} -> target {} (dist={:.2f}m)",
                         drone_ids[i], result[i], cost[i][result[i]]);
        }
    }
    spdlog::info("[Planner] Assignment total distance: {:.2f}m", total);

    return result;
}

// ============================================================
// P2: 线段-线段最短三维距离
// ============================================================
double AssemblyPlanner::PointSegmentDistance(
    double px, double py, double pz,
    double s1x, double s1y, double s1z,
    double s2x, double s2y, double s2z)
{
    double dx = s2x - s1x;
    double dy = s2y - s1y;
    double dz = s2z - s1z;
    double len_sq = dx * dx + dy * dy + dz * dz;

    if (len_sq < 1e-12) {
        // 线段退化为点
        double ddx = px - s1x, ddy = py - s1y, ddz = pz - s1z;
        return std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
    }

    // 投影参数 t
    double t = ((px - s1x) * dx + (py - s1y) * dy + (pz - s1z) * dz) / len_sq;
    t = std::clamp(t, 0.0, 1.0);

    double proj_x = s1x + t * dx;
    double proj_y = s1y + t * dy;
    double proj_z = s1z + t * dz;

    double ddx = px - proj_x, ddy = py - proj_y, ddz = pz - proj_z;
    return std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
}

double AssemblyPlanner::SegmentSegmentDistance(
    double a1x, double a1y, double a1z,
    double a2x, double a2y, double a2z,
    double b1x, double b1y, double b1z,
    double b2x, double b2y, double b2z)
{
    // 四个端点-线段距离中的最小值
    double d1 = PointSegmentDistance(a1x, a1y, a1z, b1x, b1y, b1z, b2x, b2y, b2z);
    double d2 = PointSegmentDistance(a2x, a2y, a2z, b1x, b1y, b1z, b2x, b2y, b2z);
    double d3 = PointSegmentDistance(b1x, b1y, b1z, a1x, a1y, a1z, a2x, a2y, a2z);
    double d4 = PointSegmentDistance(b2x, b2y, b2z, a1x, a1y, a1z, a2x, a2y, a2z);

    return std::min({d1, d2, d3, d4});
}

// ============================================================
// P2: 高度分离（图染色）
// ============================================================
void AssemblyPlanner::ApplyHeightSeparation(
    std::vector<AssemblyTarget>& targets,
    const std::vector<PathConflict>& conflicts,
    double layer_separation_m) const
{
    if (targets.empty() || conflicts.empty()) return;

    // 建立 drone_id -> targets 索引的映射
    std::map<int, int> drone_to_idx;
    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
        drone_to_idx[targets[i].drone_id] = i;
    }

    // 构建冲突邻接表
    std::map<int, std::set<int>> adj;
    for (const auto& c : conflicts) {
        adj[c.drone_a].insert(c.drone_b);
        adj[c.drone_b].insert(c.drone_a);
    }

    // 收集冲突图中的无人机集合并对每个连通分量分配高度
    std::set<int> visited;
    for (const auto& [drone_id, _] : adj) {
        if (visited.count(drone_id)) continue;

        // DFS 收集连通分量
        std::vector<int> component;
        std::vector<int> queue = {drone_id};
        visited.insert(drone_id);

        while (!queue.empty()) {
            int cur = queue.back();
            queue.pop_back();
            component.push_back(cur);

            for (int nb : adj[cur]) {
                if (!visited.count(nb)) {
                    visited.insert(nb);
                    queue.push_back(nb);
                }
            }
        }

        // 对该分量的每架无人机分配不同高度层
        // layer 0 保留在原高度，layer ±1,2,... 向上下分离
        if (component.size() <= 1) continue;

        spdlog::info("[Planner] Conflict component: {} drones, applying height separation",
                     component.size());

        for (int i = 0; i < static_cast<int>(component.size()); ++i) {
            auto it = drone_to_idx.find(component[i]);
            if (it == drone_to_idx.end()) continue;

            // zigzag 分配: 0, +4, -4, +8, -8, ...
            int half = i / 2;
            double offset = (i % 2 == 0) ? (-half * layer_separation_m)
                                         : ((half + 1) * layer_separation_m);

            targets[it->second].ned_z += offset;
            spdlog::info("[Planner] Drone {} height offset: {:.1f}m (layer {})",
                         component[i], offset, i);
        }
    }
}

// ============================================================
// Plan 主入口
// ============================================================
std::vector<PathConflict> AssemblyPlanner::Plan(
    const std::unordered_map<int, std::tuple<double, double, double>>& drone_positions,
    std::vector<AssemblyTarget>& targets,
    double safety_radius_m) const
{
    const double diam = 2.0 * safety_radius_m;        // 两圆柱相切的最小间距
    const double layer_m = 1.0;                        // 高度层间距 1m

    // ---- P1: 最优分配 ----
    auto assignment = AssignOptimal(targets, drone_positions);

    // 按分配重排 targets，并记录 drone_id
    std::vector<int> drone_ids;
    for (const auto& [id, _] : drone_positions) {
        drone_ids.push_back(id);
    }

    for (int i = 0; i < static_cast<int>(drone_ids.size()) && i < static_cast<int>(assignment.size()); ++i) {
        if (assignment[i] >= 0 && assignment[i] < static_cast<int>(targets.size())) {
            targets[assignment[i]].drone_id = drone_ids[i];
        }
    }

    // ---- P2: 冲突检测 ----
    std::vector<PathConflict> conflicts;
    std::vector<int> participating_ids;
    for (const auto& t : targets) {
        if (drone_positions.count(t.drone_id)) {
            participating_ids.push_back(t.drone_id);
        }
    }

    const int n = static_cast<int>(participating_ids.size());
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            int id_a = participating_ids[i];
            int id_b = participating_ids[j];

            const auto& [ax, ay, az] = drone_positions.at(id_a);
            const auto& [bx, by, bz] = drone_positions.at(id_b);

            // 查找两架无人机的目标点
            double tax = 0, tay = 0, taz = 0, tbx = 0, tby = 0, tbz = 0;
            bool found_a = false, found_b = false;
            for (const auto& t : targets) {
                if (t.drone_id == id_a) { tax = t.ned_x; tay = t.ned_y; taz = t.ned_z; found_a = true; }
                if (t.drone_id == id_b) { tbx = t.ned_x; tby = t.ned_y; tbz = t.ned_z; found_b = true; }
            }
            if (!found_a || !found_b) continue;

            double dist = SegmentSegmentDistance(
                ax, ay, az, tax, tay, taz,
                bx, by, bz, tbx, tby, tbz);

            if (dist < diam) {
                conflicts.push_back({id_a, id_b, dist});
                spdlog::info("[Planner] Conflict: drone {} ↔ {} (min_dist={:.2f}m, threshold={:.2f}m)",
                             id_a, id_b, dist, diam);
            }
        }
    }

    spdlog::info("[Planner] Detected {} path conflicts among {} drones",
                 conflicts.size(), n);

    // ---- P2: 高度分离 ----
    if (!conflicts.empty()) {
        ApplyHeightSeparation(targets, conflicts, layer_m);
    }

    return conflicts;
}
