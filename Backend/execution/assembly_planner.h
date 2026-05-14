#pragma once

#include <tuple>
#include <unordered_map>
#include <vector>

/// 集结目标点
struct AssemblyTarget {
    int    drone_id = 0;
    double ned_x = 0;
    double ned_y = 0;
    double ned_z = 0;
    int    original_index = 0;  // 该目标在原始配置中的索引
};

/// 路径冲突
struct PathConflict {
    int    drone_a = 0;
    int    drone_b = 0;
    double min_distance_m = 0;  // 两航线最短三维距离
};

/// 集结规划器
///
/// P1 最优分配: 匈牙利算法，将 N 架无人机分配给 N 个集结目标点
///              使得总飞行距离最短
///
/// P2 防碰撞: 每条飞行路径 = 当前点 → 目标点的线段 + 半径 R 的安全圆柱
///            检测两两路径的圆柱是否相交，相交则用高度分离解决
class AssemblyPlanner {
public:
    /// 执行规划
    ///
    /// @param drone_positions  无人机当前位置 map<drone_id → (ned_n, ned_e, ned_d)>
    /// @param targets          集结目标点列表（输入输出，可能被重排和修改 Z）
    /// @param safety_radius_m  安全圆柱半径（米）
    ///
    /// @return 冲突列表（用于日志/WS 推送）
    std::vector<PathConflict> Plan(
        const std::unordered_map<int, std::tuple<double, double, double>>& drone_positions,
        std::vector<AssemblyTarget>& targets,
        double safety_radius_m) const;

    // P2: 两线段最短三维距离（public 供测试用）
    static double SegmentSegmentDistance(
        double a1x, double a1y, double a1z,
        double a2x, double a2y, double a2z,
        double b1x, double b1y, double b1z,
        double b2x, double b2y, double b2z);

private:
    // P1: 构建代价矩阵并调用匈牙利
    std::vector<int> AssignOptimal(
        const std::vector<AssemblyTarget>& targets,
        const std::unordered_map<int, std::tuple<double, double, double>>& positions) const;

    // P2: 端点-线段距离
    static double PointSegmentDistance(
        double px, double py, double pz,
        double s1x, double s1y, double s1z,
        double s2x, double s2y, double s2z);

    // P2: 高度分离（图染色）
    void ApplyHeightSeparation(
        std::vector<AssemblyTarget>& targets,
        const std::vector<PathConflict>& conflicts,
        double layer_separation_m) const;
};
