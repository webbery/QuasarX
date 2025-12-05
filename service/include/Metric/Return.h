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
