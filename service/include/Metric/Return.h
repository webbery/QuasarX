#pragma once
#include "DataContext.h"

/*
 * @brief 计算动态年化回报率
 * @param flow 现金流/交易记录
 * @param context 数据上下文
 * @param mode 计算模式：0=累计年化，1=滚动年化，2=移动平均年化
 * @param window 滚动窗口大小（交易日数）
 * @return 每个时间点的年化回报率序列
 *
 * 计算原理:
 * 1. 获取每日的投资组合价值
 * 2. 计算每日收益率序列
 * 3. 根据模式计算不同维度的年化收益率
 */
float annual_return_ratio(const crash_flow_t& flow, const DataContext& context, int mode = 0, int window = YEAR_DAY);

/*
 * @brief 计算总收益率（回测简化版：无外部资金进出）
 * @param daily_values 每日组合价值序列（持仓市值）
 * @param initial_capital 初始投入本金
 * @return 总收益率 = (期末价值 - 期初价值) / 期初价值
 */
double simple_total_return(const std::vector<double>& daily_values, double initial_capital);

/*
 * @brief 计算每日收益率序列
 * @param daily_values 
 * @return 每日收益率序列，首日为 0.0
 */
Vector<double> simple_daily_return(const Vector<double>& daily_values);
/*
 * @brief 从每日收益率序列计算年化收益率
 * @param daily_returns 每日收益率序列
 * @param count 有效交易日数（用于计算 years = count / YEAR_DAY）
 * @return 年化收益率
 */
float compute_annualized_return(double total_return, int count);

std::pair<std::vector<double>, std::vector<double>>
build_portfolio_values(const crash_flow_t& flow, const DataContext& context);