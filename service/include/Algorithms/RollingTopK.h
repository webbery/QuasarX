#pragma once
#include "std_header.h"
#include <Eigen/Dense>
#include <vector>
#include <algorithm>
#include <numeric>

/**
 * rolling_topk — 滑动 top-K 元素提取
 *
 * 从一个一维时间序列中提取最大的 K 个元素，按值降序输出，并附带它们在
 * 原序列中的 0-based 索引位置。一次调用处理整个序列的 top-K，
 * 不维护滑窗状态（无状态函数，调用方自行传入当前 bar 的窗口快照）。
 *
 * 使用小顶堆（size K）维护当前 top-K：
 *   - 每次新元素 v
 *     - 堆未满（size < K）：push + sift up
 *     - 堆已满 且 v > 堆顶：替换堆顶 + sift down
 *     - 否则跳过
 *   - 最终将堆内容按值降序排序输出
 * 复杂度: O(N log K) — 对 K << N 的场景显著优于 O(N log N) 全排序
 *
 * 与 numpy.partition + sort 类似，但用显式堆实现以满足"维护小顶堆"
 * 的设计约束。
 *
 * 文件: Algorithms/RollingTopK.h
 */
namespace Alg {

struct RollingTopKResult {
    Eigen::VectorXd _values;   // top-K values, desc sorted (max first)
    Eigen::VectorXi _indices;  // corresponding 0-based indices in input
};

/**
 * @brief Extract top-K elements with their indices.
 * @param values  输入一维序列（任何长度）
 * @param k       要返回的 top 元素数量；若 k > size 则按 size 返回
 * @param desc    是否降序（默认 true）；false 时输出升序
 * @return { _values, _indices }
 */
RollingTopKResult rolling_topk(const Eigen::VectorXd& values,
                                int k,
                                bool desc = true);

/**
 * @brief Convenience: 仅有 values 的版本（不构造 Eigen 输入时使用）
 */
RollingTopKResult rolling_topk(const std::vector<double>& values,
                                int k,
                                bool desc = true);

} // namespace Alg
