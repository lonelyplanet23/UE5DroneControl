#pragma once

#include <vector>

/// 匈牙利算法 — 最小代价指派问题
///
/// 输入: N×N 方阵代价矩阵 cost[i][j] = 将任务 i 分配给工人 j 的代价
/// 输出: assignment[i] = j，即任务 i 分配给工人 j
///       总代价 = sum(cost[i][assignment[i]])
///
/// O(n³)，n ≤ 6 时几乎瞬时
class AssignmentSolver {
public:
    /// 求解最小代价指派
    /// @param cost  N×N 方阵代价矩阵
    /// @return      assignment[i] = 分配给行 i 的列索引
    static std::vector<int> HungarianMinCost(
        const std::vector<std::vector<double>>& cost);
};
