#pragma once
#include <vector>
#include <string>
#include <Eigen/Dense>
#include "Metric/CUSUMDetector.h"

/*
 * @brief 计算 VaR (Value at Risk，风险价值)
 * @param returns 日收益率序列
 * @param confidence_level 置信度，如 0.95 表示 95% VaR
 * @return VaR 值（正值，表示在该置信度下的最大日损失）
 */
float compute_var(const std::vector<double>& returns, double confidence_level = 0.95);

/*
 * @brief 计算 CVaR (Conditional VaR，预期短缺)
 * @param returns 日收益率序列
 * @param confidence_level 置信度，如 0.95
 * @return CVaR 值（正值，表示超过 VaR 阈值的平均损失）
 */
float compute_cvar(const std::vector<double>& returns, double confidence_level = 0.95);

/*
 * @brief 计算收益率序列的滞后 k 阶自相关系数
 * @param returns 日收益率序列
 * @param lag 滞后阶数，默认 lag=1（检验相邻收益率的相关性）
 * @return 自相关系数 [-1, 1]
 *
 * 判断标准：
 *   |acf| < 0.1  → 无显著自相关，使用普通 Bootstrap
 *   |acf| >= 0.1 → 存在自相关，使用 Block Bootstrap
 */
double compute_autocorrelation(const std::vector<double>& returns, int lag = 1);

/*
 * @brief 计算 EWMA（指数加权移动平均）VaR
 * @param returns 日收益率序列
 * @param confidence_level 置信度
 * @param decay 衰减因子（默认 0.94，RiskMetrics 推荐值）
 * @return VaR 值（正值）
 *
 * EWMA 给近期数据更高权重，对分布变化更敏感。
 * 公式：σ²_t = λ * σ²_{t-1} + (1-λ) * r²_{t-1}
 */
float compute_ewma_var(const std::vector<double>& returns, double confidence_level = 0.95,
                       double decay = 0.94);

/*
 * @brief 自适应 VaR 配置
 *
 * 默认行为：
 *   - 无变点：历史模拟法，250 天窗口，95% 置信度
 *   - 有变点：EWMA 加权 VaR，60 天窗口，99% 置信度
 */
struct AdaptiveVaRConfig {
    double confidence = 0.95;           // 基础置信度
    size_t normal_window = 250;         // 正常回看窗口（交易日）
    size_t stressed_window = 60;        // 变点后回看窗口
    double stressed_confidence = 0.99;  // 变点后置信度
    double ewma_decay = 0.94;           // EWMA 衰减因子
    bool enable_ewma_fallback = true;   // 变点后是否切换 EWMA
};

/*
 * @brief 基于 CUSUM 的自适应 VaR 计算
 *
 * 逻辑：
 *   1. 运行 CUSUM 检测识别变点
 *   2. 如果最后 N 天内有变点 → 使用 stressed_window + stressed_confidence + EWMA
 *   3. 否则 → 使用 normal_window + confidence + 历史模拟法
 *
 * @param returns 完整日收益率序列
 * @param detector CUSUM 检测器（已运行或重新运行）
 * @param config 自适应配置
 * @return VaR 值（正值）
 */
float compute_adaptive_var(const std::vector<double>& returns,
                           const CUSUMDetector& detector,
                           const AdaptiveVaRConfig& config = {});

/*
 * @brief 凸性 VaR 计算结果
 */
struct ConvexityVaRResult {
    double var_ewma = 0.0;       // EWMA VaR
    double var_convexity = 0.0;  // 凸性调整后 VaR
    double kurtosis = 0.0;       // 超额峰度
    double alpha = 1.0;          // 自动推断的 α
    double drift_ratio = 0.0;    // CUSUM 归一化漂移（0~1）
};

/*
 * @brief 凸性 VaR 附加计算
 *
 * 公式：var_convexity = var_base + α × drift_ratio × max(var_ewma - var_base, 0)
 *
 * - α 由超额峰度自动推断：α = 1.0 + clamp(kurtosis/3, -0.5, 1.0)
 * - drift_ratio = |S+ - S-| / h，归一化到 [0, 1]
 * - 只在 EWMA VaR > 历史 VaR 时加附加（风险被低估时）
 *
 * @param returns 日收益率序列
 * @param detector CUSUM 检测器（已运行）
 * @param confidence VaR 置信度（默认 0.95）
 * @param ewma_decay EWMA 衰减因子（默认 0.94）
 * @return ConvexityVaRResult 完整结果
 */
ConvexityVaRResult compute_convexity_var(
    const std::vector<double>& returns,
    const CUSUMDetector& detector,
    double confidence = 0.95,
    double ewma_decay = 0.94);

/*
 * @brief 计算收益率序列的超额峰度
 * @param returns 收益率序列
 * @return 超额峰度 κ = E[(r-μ)⁴]/σ⁴ - 3
 */
double compute_excess_kurtosis(const std::vector<double>& returns);

/*
 * @brief 计算两个收益率向量的相关系数
 * @param x 收益率序列 1
 * @param y 收益率序列 2
 * @return 相关系数 [-1, 1]
 */
double compute_correlation(const std::vector<double>& x, const std::vector<double>& y);

/*
 * @brief 计算收益率矩阵的相关系数矩阵
 * @param data 输入矩阵 (n_assets × n_samples)，每行是一个资产的收益率序列
 * @return 相关系数矩阵 (n_assets × n_assets)
 */
Eigen::MatrixXd compute_correlation_matrix(const Eigen::MatrixXd& data);
