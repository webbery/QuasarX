#pragma once
#include "std_header.h"

namespace finance {

/**
 * @brief 三阶段增长模型
 */
double stage3GM(double g1, double g2, double D, double T1, double T2, double r);

/**
 * @brief 计算 Kyle's Lambda（订单流对价格的冲击系数）
 *
 * 通过线性回归 ΔP = λ · OF + ε 估计市场微观结构中的逆向选择成本。
 *
 * @param prices      价格序列（按时间顺序，至少2个元素）
 * @param volumes     成交量序列（与prices等长）
 * @param trade_side  本次交易方向: 0=BUY, 1=SELL
 * @param trade_volume 本次交易量
 * @return Kyle's Lambda。数据不足或方差为0时返回 0
 *
 * 注：因缺乏逐笔成交方向，用价格变动方向代理订单流符号：
 *      dp > 0 → direction = +1（买方发起）
 *      dp < 0 → direction = -1（卖方发起）
 */
double kyles_lambda(const Vector<double>& prices,
                    const Vector<int64_t>& volumes,
                    int trade_side,
                    int64_t trade_volume);

/**
 * @brief 计算 Amihud 不流动性指标
 *
 * Amihud = |R| / Volume，衡量单位成交量引起的收益率绝对变化。
 *
 * @param prices  价格序列
 * @param volumes 成交量序列（与prices等长）
 * @return 平均 Amihud 值。数据不足时返回 0
 */
double amihud_illiquidity(const Vector<double>& prices,
                          const Vector<int64_t>& volumes);

}

bool LoadStockQuote(DataFrame& df, const String& path);