#pragma once

// MVO 的"分支算法参数"枚举。
//
// 单独成文件的原因：PortfolioNode.h 需要这些枚举来记配置，但不应为了几个
// enum 就把 kalman（Eigen 模板）和 OSQP 头文件拖进所有包含它的编译单元
// （Strategy.cpp / AgentSubSystem.cpp / server.cpp / BacktestHandler.cpp ...）。
// 重实现（MeanEstimator / RiskModel / MvoOptimizer）只在 PortfolioNode.cpp 里
// 以 unique_ptr 持有，走前向声明。

namespace mvo {

/**
 * @brief 均值估计方法
 *
 * MVO 的成败几乎完全取决于均值估计：日频收益率的样本均值噪声极大
 * （σ_daily ≈ 2.5% 时，60 日样本均值的标准误 ≈ 0.32%/日，年化约 50%），
 * 比它要估的 alpha 本身还大。直接送进 MVO，权重基本由噪声决定
 * （Markowitz 病）。lab 实测：样本均值夏普 1.27，等权基线 1.27，完全没有提升。
 */
enum class MeanMethod {
    Kalman,   ///< 卡尔曼滤波估计时变均值（默认）
    Sample    ///< 滚动样本均值（对照组）
};

/**
 * @brief 协方差估计方法
 *
 * 样本协方差在 n 大、t 小时噪声不小（n=33、T=1137 时相对误差约
 * √(2/T) ≈ 4%），配上 MVO 的"取最大"倾向会被放大成极端权重。
 */
enum class CovMethod {
    EwmaShrunk,   ///< EWMA + 常数相关收缩 + PSD（默认）
    Sample,       ///< 样本协方差（无收缩，对照组）
    LedoitWolf    ///< Ledoit-Wolf OAS 收缩（复用 Util/finance.h）
};

/**
 * @brief 优化器方法
 */
enum class OptimizerMethod {
    Ospq          ///< OSQP（ADMM 一阶算子分裂，专解凸 QP）
};

}  // namespace mvo
