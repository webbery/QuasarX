#pragma once
#include "DataContext.h"

/*
 * @brief 计算最大回撤
 * @param flow 现金流/交易记录
 * @param context 数据上下文
 * @return 最大回撤 (0 到 1 之间的小数，如 0.2 表示 20% 回撤)
 *
 * 计算原理:
 * 1. 计算每日投资组合价值
 * 2. 追踪历史最高价值
 * 3. 计算每个时点的回撤 = (最高价值 - 当前价值) / 最高价值
 * 4. 返回最大回撤值
 */
float max_drawdown_ratio(const Vector<double>& portfolio_values);

/*
 * @brief 计算总收益率
 * @param flow 现金流/交易记录
 * @param context 数据上下文
 * @return 总收益率 (如 0.25 表示 25% 收益)
 *
 * 计算原理:
 * 1. 计算期末和期初的投资组合价值
 * 2. 考虑现金流影响
 * 3. 返回总收益率
 */
float total_return_ratio(const crash_flow_t& flow, const DataContext& context);

/*
 * @brief 计算胜率
 * @return 胜率 (0 到 1 之间的小数，如 0.6 表示 60% 胜率)
 *
 * 计算原理:
 * 3. 胜率 = 盈利交易数 / 总交易数
 */
float win_rate(const Vector<double>& daily_values);

/*
 * @brief 计算卡玛比率 (Calmar Ratio)
 * 计算原理:
 * 卡玛比率 = 年化收益率 / 最大回撤绝对值
 */
float calmar_ratio(double annual_return, double max_dd);
