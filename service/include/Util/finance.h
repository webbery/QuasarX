#pragma once
#include "std_header.h"

namespace finance {

/**
 * @brief 三阶段增长模型
 */
double stage3GM(double g1, double g2, double D, double T1, double T2, double r);

/**
 * @brief 计算 Kyle's Lambda（订单流对价格的冲击系数）
 */
double kyles_lambda(const Vector<double>& prices,
                    const Vector<int64_t>& volumes,
                    int trade_side,
                    int64_t trade_volume);

/**
 * @brief 计算 Amihud 不流动性指标
 */
double amihud_illiquidity(const Vector<double>& prices,
                          const Vector<int64_t>& volumes);

// ──────────────────────────────────────────────────────────────────────
// 信号分析 / 时序分析工具函数
// ──────────────────────────────────────────────────────────────────────

/**
 * @brief 计算自相关函数 (ACF)
 * @param data     输入序列
 * @param max_lag  最大滞后阶数
 * @return         ACF 值，索引 0 = lag 0（总是 1.0）
 */
Vector<double> computeACF(const Vector<double>& data, int max_lag);

/**
 * @brief 计算偏自相关函数 (PACF)，使用 Durbin-Levinson 算法
 * @param acf      自相关函数值（从 computeACF 获得）
 * @param max_lag  最大滞后阶数
 * @return         PACF 值，索引 0 = lag 0
 */
Vector<double> computePACF(const Vector<double>& acf, int max_lag);

/**
 * @brief 估计序列的平均周期（ACF 第一个过零点 × 2）
 * @param data  输入序列
 * @return      平均周期（bar 数），无法估计时返回 0
 */
double estimateMeanPeriod(const Vector<double>& data);

/**
 * @brief 计算能量占比（序列方差 / 原始信号方差）
 * @param component  分量序列（如 IMF）
 * @param original   原始信号序列
 * @return           能量占比 [0, 1]
 */
double computeEnergyPct(const Vector<double>& component,
                         const Vector<double>& original);

}

bool LoadStockQuote(DataFrame& df, const String& path);