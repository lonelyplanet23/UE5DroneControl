#include <gtest/gtest.h>
#include "execution/assembly_planner.h"
#include "conversion/assignment_solver.h"

// ============================================================
// 匈牙利算法
// ============================================================
TEST(AssignmentSolverTest, Hungarian3x3)
{
    // 3 架无人机 → 3 个目标
    // d0→t0=1, d0→t1=5, d0→t2=3
    // d1→t0=5, d1→t1=1, d1→t2=5
    // d2→t0=3, d2→t1=5, d2→t2=1
    // 最优: d0→t0, d1→t1, d2→t2, total=3
    std::vector<std::vector<double>> cost = {
        {1, 5, 3},
        {5, 1, 5},
        {3, 5, 1},
    };
    auto assignment = AssignmentSolver::HungarianMinCost(cost);
    ASSERT_EQ(assignment.size(), 3u);
    // 验证每个目标被分配一次
    std::set<int> targets;
    double total = 0;
    for (int i = 0; i < 3; ++i) {
        ASSERT_GE(assignment[i], 0);
        targets.insert(assignment[i]);
        total += cost[i][assignment[i]];
    }
    EXPECT_EQ(targets.size(), 3u);
    EXPECT_NEAR(total, 3.0, 1e-6);
}

TEST(AssignmentSolverTest, HungarianDiagonal)
{
    std::vector<std::vector<double>> cost = {
        {10, 2, 8},
        {9, 10, 1},
        {5, 6, 7},
    };
    auto a = AssignmentSolver::HungarianMinCost(cost);
    ASSERT_EQ(a.size(), 3u);
    std::set<int> s;
    for (int v : a) s.insert(v);
    EXPECT_EQ(s.size(), 3u);
}

// ============================================================
// 线段距离
// ============================================================
TEST(SegmentDistanceTest, Parallel)
{
    // seg A: (0,0,0)→(10,0,0)
    // seg B: (0,5,0)→(10,5,0) → 距离 = 5
    double d = AssemblyPlanner::SegmentSegmentDistance(
        0,0,0, 10,0,0,  0,5,0, 10,5,0);
    EXPECT_NEAR(d, 5.0, 1e-6);
}

TEST(SegmentDistanceTest, Crossing)
{
    // seg A: (0,0,0)→(10,0,0)
    // seg B: (5,-5,0)→(5,5,0) → 距离 = 0 (交叉在 XY)
    double d = AssemblyPlanner::SegmentSegmentDistance(
        0,0,0, 10,0,0,  5,-5,0, 5,5,0);
    EXPECT_NEAR(d, 0.0, 1e-4);
}

TEST(SegmentDistanceTest, EndpointClosest)
{
    // seg A 端点靠近 seg B
    double d = AssemblyPlanner::SegmentSegmentDistance(
        0,0,0, 0,0,0,  10,0,0, 20,0,0);
    EXPECT_NEAR(d, 10.0, 1e-6);
}

TEST(SegmentDistanceTest, VerticalSeparation)
{
    // 水平交叉但垂直分离
    double d = AssemblyPlanner::SegmentSegmentDistance(
        0,0,0, 10,0,0,  5,-5,10, 5,5,10);
    // 垂直距离 10m
    EXPECT_GT(d, 8.0);
    EXPECT_LT(d, 12.0);
}

// ============================================================
// 规划器集成
// ============================================================
TEST(AssemblyPlannerTest, NoConflictSparse)
{
    AssemblyPlanner planner;
    std::unordered_map<int, std::tuple<double, double, double>> positions = {
        {1, {0, 0, -5}},
        {2, {50, 0, -5}},
    };
    std::vector<AssemblyTarget> targets = {
        {1, 0, 0, -5, 0},
        {2, 50, 0, -5, 1},
    };

    auto conflicts = planner.Plan(positions, targets, 2.0);
    // 两机距离 50m，远大于 4m 阈值
    EXPECT_TRUE(conflicts.empty());
    // targets 不应被修改
    EXPECT_NEAR(targets[0].ned_z, -5.0, 1e-6);
    EXPECT_NEAR(targets[1].ned_z, -5.0, 1e-6);
}

TEST(AssemblyPlannerTest, ConflictHeightSeparation)
{
    AssemblyPlanner planner;
    // 两架无人机相距 5m，路线交叉
    std::unordered_map<int, std::tuple<double, double, double>> positions = {
        {1, {0, 0, -10}},
        {2, {5, 0, -10}},
    };
    std::vector<AssemblyTarget> targets = {
        {1, 5, 0, -10, 0},
        {2, 0, 0, -10, 1},
    };

    double original_z_a = targets[0].ned_z;
    double original_z_b = targets[1].ned_z;

    auto conflicts = planner.Plan(positions, targets, 2.0);
    // 交叉段距离 ≈ 0 < 4m → 应检测到冲突
    EXPECT_GE(conflicts.size(), 1u);

    // 应用高度分离后，至少有一架的 Z 改变了
    bool z_changed = (std::abs(targets[0].ned_z - original_z_a) > 0.1) ||
                     (std::abs(targets[1].ned_z - original_z_b) > 0.1);
    EXPECT_TRUE(z_changed);
}
