#pragma once
#include <vector>

/*
 * @brief 计算年化波动率
 * @param daily_returns 每日收益率序列
 * @return 年化波动率 = std(daily_returns) * sqrt(252)
 */
double compute_annualized_volatility(const std::vector<double>& daily_returns);

/*
 * @brief 滚动波动率
 * @param returns   收益率序列
 * @param window    滚动窗口大小
 * @return 年化波动率序列（长度 = returns.size() - window + 1）
 */
std::vector<double> rolling_volatility(const std::vector<double>& returns, int window);
