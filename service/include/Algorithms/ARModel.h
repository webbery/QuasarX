#pragma once
#include "std_header.h"
#include <vector>

namespace ar_model {

// 置信带系数 (95%)
constexpr double CONFIDENCE_Z = 1.96;
// 最大预测步数
constexpr int MAX_FORECAST_STEPS = 10;

/**
 * @brief AR 拟合结果
 */
struct ARFit {
    std::vector<double> coeffs;   // φ₁, φ₂, ..., φₚ
    double residual_var = 0;      // 残差方差 σ²
    bool stable = true;           // 特征根是否全部 < 1
    int order_p = 0;
};

/**
 * @brief 预测结果
 */
struct Forecast {
    std::vector<double> values;        // 预测值 (h 步)
    std::vector<double> std_per_step;  // 每步预测标准差
};

/**
 * @brief 完整预测结果（含置信区间）
 */
struct ForecastResult {
    std::string source_series;         // "returns" | "rolling_vol"
    int order_p = 0;
    std::vector<double> ar_coeffs;
    double residual_var = 0;
    std::vector<double> forecast_values;
    std::vector<double> forecast_upper_1sigma;
    std::vector<double> forecast_lower_1sigma;
    std::vector<double> forecast_upper_2sigma;
    std::vector<double> forecast_lower_2sigma;
    std::vector<double> forecast_std;  // 每步标准差（前端渐变用）
    bool has_autocorrelation = false;
    String note;                       // 诊断信息
};

/**
 * @brief 找到 ACF 中最后一个显著 lag
 * @param acf   ACF 值（含 lag 0）
 * @param N     原始序列长度
 * @param max_p 最大阶数上限 (默认 10)
 * @return 显著 lag 数，无则返回 0
 */
int findSignificantLag(const std::vector<double>& acf, int N, int max_p = MAX_FORECAST_STEPS);

/**
 * @brief Levinson-Durbin 递推求解 AR(p) 系数
 * @param acf ACF 值
 * @param p   AR 阶数
 * @return 拟合结果（含稳定性检查）
 */
ARFit solveYuleWalker(const std::vector<double>& acf, int p);

/**
 * @brief AR(p) 向前预测
 * @param coeffs  AR 系数
 * @param history 最近 p 个观测值（从旧到新）
 * @param steps   预测步数
 * @param var     残差方差
 * @return 预测值 + 每步标准差
 */
Forecast predict(const std::vector<double>& coeffs,
                 const std::vector<double>& history,
                 int steps, double var);

/**
 * @brief 构建完整预测结果（含置信区间）
 * @param series     原始序列（收益率或 rolling_vol）
 * @param acf        ACF 值
 * @param max_p      最大 AR 阶数
 * @param source     来源标签 ("returns" / "rolling_vol")
 * @param last_known 最后一个已知值（用于推导预测价格/波动率）
 * @return 预测结果
 */
ForecastResult buildForecast(const std::vector<double>& series,
                              const std::vector<double>& acf,
                              int max_p,
                              const String& source,
                              double last_known = 0.0);

/**
 * @brief 多资产协方差外推
 * @param base_cov   历史协方差矩阵 (N×N)
 * @param forecasts  各资产 Forecast 结果
 * @param horizon    外推步数
 * @return 外推后的协方差矩阵
 *
 * 原理: Σ_forecast(h) = Σ_base ⊙ Γ(h)
 * Γ(h)_ij = std_i(h) × std_j(h) 膨胀因子
 */
std::vector<std::vector<double>> extrapolateCovariance(
    const std::vector<std::vector<double>>& base_cov,
    const std::vector<Forecast>& forecasts,
    int horizon);

}  // namespace ar_model
