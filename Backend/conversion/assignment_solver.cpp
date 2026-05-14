#include "assignment_solver.h"
#include <algorithm>
#include <cmath>
#include <limits>

std::vector<int> AssignmentSolver::HungarianMinCost(
    const std::vector<std::vector<double>>& cost)
{
    const int n = static_cast<int>(cost.size());
    if (n == 0) return {};
    if (n == 1) return {0};

    // 处理矩形矩阵：填充为方阵（用大值填充）
    const int m = static_cast<int>(cost[0].size());
    const int dim = std::max(n, m);

    constexpr double kInf = 1e18;

    std::vector<std::vector<double>> a(dim, std::vector<double>(dim, kInf));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j)
            a[i][j] = cost[i][j];

    // u[row] + v[col] 势函数
    std::vector<double> u(dim + 1, 0), v(dim + 1, 0);
    std::vector<int> p(dim + 1, 0);      // p[col] = 匹配到 col 的行
    std::vector<int> way(dim + 1, 0);    // way[col] = 前驱行

    for (int i = 1; i <= dim; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(dim + 1, kInf);
        std::vector<bool> used(dim + 1, false);

        do {
            used[j0] = true;
            int i0 = p[j0];
            double delta = kInf;
            int j1 = 0;

            for (int j = 1; j <= dim; ++j) {
                if (!used[j]) {
                    double cur = a[i0 - 1][j - 1] - u[i0] - v[j];
                    if (cur < minv[j]) {
                        minv[j] = cur;
                        way[j] = j0;
                    }
                    if (minv[j] < delta) {
                        delta = minv[j];
                        j1 = j;
                    }
                }
            }

            for (int j = 0; j <= dim; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);

        // 增广
        do {
            int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    // 输出 assignment[i] = 列索引
    std::vector<int> assignment(n + 1, -1);
    for (int j = 1; j <= dim; ++j) {
        if (p[j] >= 1 && p[j] <= n) {
            int col = j - 1;
            if (col < m) {
                assignment[p[j] - 1] = col;
            }
        }
    }

    // 后备：未匹配的行贪心分配
    for (int i = 0; i < n; ++i) {
        if (assignment[i] == -1) {
            double best = kInf;
            int best_j = -1;
            for (int j = 0; j < m; ++j) {
                if (cost[i][j] < best) {
                    bool used = false;
                    for (int ii = 0; ii < n; ++ii)
                        if (assignment[ii] == j) { used = true; break; }
                    if (!used) { best = cost[i][j]; best_j = j; }
                }
            }
            assignment[i] = best_j;
        }
    }

    return std::vector<int>(assignment.begin(), assignment.begin() + n);
}
