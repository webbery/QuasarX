#pragma once
#include "DataContext.h"

/*
 * @brief 计算夏普比率.计算原理:将现金流发生的时点作为分割点，将整个期间分成若干个子区间，每个子区间内没有现金流
          计算每个子区间的收益率，然后将子区间收益率链接起来
 * @param flow 现金流/交易记录
 * @param context 数据上下文
 * @param freerate 无风险利率（年化）
 * @return 夏普比率
 */
float sharp_ratio(const crash_flow_t& flow, const DataContext& context, double freerate);

/*
 * @brief 计算年化波动率
 * @param daily_returns 每日收益率序列
 * @return 年化波动率（标准差）= std(daily_returns) * sqrt(YEAR_DAY)
 */
float compute_annualized_volatility(const Vector<double>& daily_returns);

/*
 * @brief 计算夏普比率（简化版）
 * @param annualized_return 年化收益率
 * @param annualized_volatility 年化波动率
 * @param risk_free_rate 无风险利率（年化）
 * @return 夏普比率 = (年化收益率 - 无风险利率) / 年化波动率
 */
float compute_sharp_ratio(float annualized_return, float annualized_volatility, double risk_free_rate);