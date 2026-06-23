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
// 时间序列分析工具函数（多标的）
// ──────────────────────────────────────────────────────────────────────

/// OLS 回归结果
struct OLSResult {
    double alpha;              // 截距
    double beta;               // 斜率
    Vector<double> residuals;  // 残差序列
    double r_squared;          // R²
    double std_error;          // 回归标准误差
};

/// 交叉相关分析结果
struct CrossCorrelationResult {
    Vector<double> ccf;        // lag -max_lag ~ +max_lag 的相关系数
    int max_lag_index;         // CCF 向量中最大相关的索引
    double max_correlation;    // 最大 |相关系数|
    int lead_lag;              // >0: y领先x, <0: x领先y
};

/// 格兰杰因果检验结果
struct GrangerCausalityResult {
    double f_statistic;
    double p_value;
    bool is_significant;       // p < 0.05
    int optimal_lag;           // AIC 最小的滞后阶数
    String direction;          // "X→Y" 或 "Y→X"
};

/// 协整检验结果 (Engle-Granger)
struct CointegrationResult {
    double beta;               // y = α + βx + ε
    double alpha;
    double adf_statistic;      // 残差 ADF 检验统计量
    double p_value;
    bool is_cointegrated;      // p < 0.05
    double half_life;          // 均值回归半衰期 (bar 数)
};

/// OLS 回归: y = α + βx + ε
OLSResult olsRegression(const Vector<double>& x, const Vector<double>& y);

/// 交叉相关函数 (Cross-Correlation Function)
/// 计算 x 和 y 在滞后 [-max_lag, +max_lag] 下的相关系数
/// 正值 lag: y 领先 x；负值 lag: x 领先 y
CrossCorrelationResult crossCorrelation(
    const Vector<double>& x, const Vector<double>& y, int max_lag);

/// 格兰杰因果检验
/// 检验 y 是否是 x 的格兰杰原因
GrangerCausalityResult grangerCausalityTest(
    const Vector<double>& x, const Vector<double>& y, int max_lag,
    const String& x_name = "X", const String& y_name = "Y");

/// Engle-Granger 协整检验
/// 检验 x 和 y 是否存在长期均衡关系
CointegrationResult engleGrangerTest(
    const Vector<double>& x, const Vector<double>& y);

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