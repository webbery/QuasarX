#pragma once
#include <vector>
#include <string>

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
