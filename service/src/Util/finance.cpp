#include "Util/finance.h"
#include "Util/datetime.h"
#include "Algorithms/EMD_SIMD.h"
#include <boost/math/statistics/univariate_statistics.hpp>
#include <filesystem>
#include "csv.h"
#include <cmath>
#include <numbers>
#include <numeric>
#include <algorithm>

namespace finance {

// ──────────────────────────────────────────────────────────────────────
// 内部工具函数
// ──────────────────────────────────────────────────────────────────────

/// 计算均值
static double calcMean(const Vector<double>& data) {
    if (data.empty()) return 0.0;
    double sum = 0;
    for (auto v : data) sum += v;
    return sum / data.size();
}

/// 计算样本标准差
static double calcStdDev(const Vector<double>& data, double mean_val = -1) {
    if (data.size() < 2) return 0.0;
    double m = (mean_val >= 0) ? mean_val : calcMean(data);
    double ss = 0;
    for (auto v : data) {
        double d = v - m;
        ss += d * d;
    }
    return std::sqrt(ss / (data.size() - 1));
}

// ──────────────────────────────────────────────────────────────────────
// OLS 回归
// ──────────────────────────────────────────────────────────────────────

OLSResult olsRegression(const Vector<double>& x, const Vector<double>& y) {
    OLSResult result{};
    size_t n = x.size();
    if (n < 2 || y.size() != n) return result;

    double mx = calcMean(x), my = calcMean(y);
    double sxy = 0, sxx = 0;
    for (size_t i = 0; i < n; ++i) {
        double dx = x[i] - mx;
        sxy += dx * (y[i] - my);
        sxx += dx * dx;
    }

    if (sxx < 1e-15) {
        result.alpha = my;
        result.beta = 0;
        result.r_squared = 0;
        result.std_error = calcStdDev(y);
        result.residuals.assign(n, 0);
        for (size_t i = 0; i < n; ++i) result.residuals[i] = y[i] - my;
        return result;
    }

    result.beta = sxy / sxx;
    result.alpha = my - result.beta * mx;

    // 残差和R²
    double ss_res = 0, ss_tot = 0;
    result.residuals.resize(n);
    for (size_t i = 0; i < n; ++i) {
        double predicted = result.alpha + result.beta * x[i];
        result.residuals[i] = y[i] - predicted;
        ss_res += result.residuals[i] * result.residuals[i];
        double dy = y[i] - my;
        ss_tot += dy * dy;
    }
    result.r_squared = (ss_tot > 1e-15) ? (1.0 - ss_res / ss_tot) : 0.0;
    result.std_error = (n > 2) ? std::sqrt(ss_res / (n - 2)) : 0.0;

    return result;
}

// ──────────────────────────────────────────────────────────────────────
// 交叉相关函数 (CCF)
// ──────────────────────────────────────────────────────────────────────

CrossCorrelationResult crossCorrelation(
    const Vector<double>& x, const Vector<double>& y, int max_lag)
{
    CrossCorrelationResult result{};
    size_t n = x.size();
    if (n < 3 || y.size() != n || max_lag < 1) return result;

    double mx = calcMean(x), my = calcMean(y);
    double sx = calcStdDev(x, mx), sy = calcStdDev(y, my);
    double denom = sx * sy;
    if (denom < 1e-15) return result;

    int total_lags = 2 * max_lag + 1;
    result.ccf.resize(total_lags);
    result.max_correlation = 0;
    result.max_lag_index = max_lag;  // lag=0 的索引

    for (int lag = -max_lag; lag <= max_lag; ++lag) {
        int idx = lag + max_lag;
        double sum_xy = 0;
        int count = 0;

        for (int i = 0; i < (int)n; ++i) {
            int j = i + lag;
            if (j >= 0 && j < (int)n) {
                sum_xy += (x[i] - mx) * (y[j] - my);
                ++count;
            }
        }

        result.ccf[idx] = (count > 0) ? (sum_xy / (count * denom)) : 0.0;

        double abs_corr = std::abs(result.ccf[idx]);
        if (abs_corr > result.max_correlation) {
            result.max_correlation = abs_corr;
            result.max_lag_index = idx;
            result.lead_lag = lag;
        }
    }

    return result;
}

// ──────────────────────────────────────────────────────────────────────
// ADF 检验 (内部函数)
// ──────────────────────────────────────────────────────────────────────

/// Augmented Dickey-Fuller 检验
/// 回归: Δy_t = α + β·y_{t-1} + Σγ_i·Δy_{t-i} + ε
/// 检验 H0: β = 0 (存在单位根)
/// 返回 {adf_statistic, p_value}
static std::pair<double, double> adfTest(
    const Vector<double>& series, int max_lag = 0)
{
    size_t n = series.size();
    if (n < (size_t)(max_lag + 10)) return {0.0, 1.0};

    // 构造差分和滞后差分项
    Vector<double> dy;         // Δy_t
    Vector<double> y_lag;      // y_{t-1}
    Vector<Vector<double>> lag_diffs;  // Δy_{t-i}

    for (size_t t = 1 + max_lag; t < n; ++t) {
        dy.push_back(series[t] - series[t - 1]);
        y_lag.push_back(series[t - 1]);
    }

    size_t m = dy.size();
    if (m < 10) return {0.0, 1.0};

    // 对每个滞后阶数，构造滞后差分项
    for (int p = 0; p < max_lag; ++p) {
        Vector<double> ld;
        for (size_t t = 1 + max_lag; t < n; ++t) {
            size_t idx = t - 1 - p;
            if (idx > 0 && idx < n) {
                ld.push_back(series[idx] - series[idx - 1]);
            }
        }
        if (ld.size() == m) lag_diffs.push_back(ld);
    }

    // 受限回归 (无滞后差分): Δy_t = α + β·y_{t-1} + ε
    auto ols_basic = olsRegression(y_lag, dy);
    double beta = ols_basic.beta;
    double se_beta = ols_basic.std_error;

    // ADF 统计量
    double adf_stat = (se_beta > 1e-15) ? (beta / se_beta) : 0.0;

    // 使用 Mackinnon 近似临界值 (样本量 > 25)
    // 简化版: 根据 ADF 统计量查表近似 p 值
    // Mackinnon (1996) 响应面近似
    double n_eff = (double)m;
    // 5% 临界值约 -2.86 (有截距无趋势), 1% 约 -3.43, 10% 约 -2.57
    double cv_1pct = -3.43, cv_5pct = -2.86, cv_10pct = -2.57;

    double p_value;
    if (adf_stat <= cv_1pct) p_value = 0.01;
    else if (adf_stat <= cv_5pct) {
        // 线性插值
        double t = (adf_stat - cv_1pct) / (cv_5pct - cv_1pct);
        p_value = 0.01 + t * 0.04;
    }
    else if (adf_stat <= cv_10pct) {
        double t = (adf_stat - cv_5pct) / (cv_10pct - cv_5pct);
        p_value = 0.05 + t * 0.05;
    }
    else {
        // 大于10%临界值，用指数衰减近似
        double excess = adf_stat - cv_10pct;
        p_value = 0.10 + 0.90 * (1.0 - std::exp(-excess * 2.0));
        if (p_value > 1.0) p_value = 1.0;
    }

    return {adf_stat, p_value};
}

// ──────────────────────────────────────────────────────────────────────
// F 分布 CDF 近似 (内部函数)
// ──────────────────────────────────────────────────────────────────────

/// 使用正则不完全 Beta 函数近似 F 分布 CDF
/// 简化实现: 对大自由度用正态近似
static double fDistributionCDF(double f_val, double df1, double df2) {
    if (f_val <= 0) return 0.0;
    if (df1 < 1 || df2 < 1) return 0.5;

    // x = df1 * f / (df1 * f + df2)
    double x = df1 * f_val / (df1 * f_val + df2);

    // 对大自由度用 Wilson-Hilferty 近似
    // F ≈ ((1 - 2/(9*df2)) / (1 - 2/(9*df1)) * (1 + z*sqrt(2/(9*df1)))^3 ... 太复杂
    // 简化: 用 log 近似
    // 对于 df1, df2 > 10, F 分布近似卡方/df1

    // 更实用的近似: 利用 Beta 不完全函数的连分式展开的简化版
    // 这里用一个实用的经验近似

    // 当 df2 很大时，df1*F 近似 χ²(df1)
    if (df2 > 1000) {
        // χ² 近似用正态: (χ² - df) / sqrt(2*df) ~ N(0,1)
        double z = (df1 * f_val - df1) / std::sqrt(2.0 * df1);
        // 标准正态 CDF 近似
        double phi = 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));
        return phi;
    }

    // 一般情况: 用连分式近似的简化版本
    // 参考: Abramowitz and Stegun 26.5.2
    // I_x(a,b) ≈ x^a (1-x)^b / (a*B(a,b)) * 连分式
    // 这里用简化的数值近似

    // 使用正态近似作为 fallback
    double mean_f = df2 / (df2 - 2.0);  // E[F], df2 > 2
    double var_f = (2.0 * df2 * df2 * (df1 + df2 - 2.0)) /
                   (df1 * (df2 - 2.0) * (df2 - 2.0) * (df2 - 4.0));
    if (df2 <= 4.0) var_f = df2 * df2 / (df1 * (df2 - 2.0));

    double z = (f_val - mean_f) / std::sqrt(std::max(var_f, 1e-15));
    double phi = 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));
    return std::min(std::max(phi, 0.0), 1.0);
}

/// F 分布的右尾概率 (p-value)
static double fDistributionPValue(double f_val, double df1, double df2) {
    return 1.0 - fDistributionCDF(f_val, df1, df2);
}

// ──────────────────────────────────────────────────────────────────────
// 格兰杰因果检验
// ──────────────────────────────────────────────────────────────────────

GrangerCausalityResult grangerCausalityTest(
    const Vector<double>& x, const Vector<double>& y, int max_lag,
    const String& x_name, const String& y_name)
{
    GrangerCausalityResult result{};
    result.direction = y_name + "→" + x_name;
    size_t n = x.size();
    if (n < (size_t)(max_lag * 2 + 10) || y.size() != n) return result;

    double best_aic = std::numeric_limits<double>::max();
    result.optimal_lag = 1;

    for (int p = 1; p <= max_lag; ++p) {
        // 受限模型: x_t = α + Σβ_i·x_{t-i} + ε_r
        // 非受限模型: x_t = α + Σβ_i·x_{t-i} + Σγ_i·y_{t-i} + ε_u

        Vector<double> y_vec;  // 因变量
        Vector<double> x_lags[10];  // x 的滞后
        Vector<double> y_lags[10];  // y 的滞后

        for (size_t t = p; t < n; ++t) {
            y_vec.push_back(x[t]);
            for (int i = 0; i < p; ++i) {
                if (x_lags[i].size() == 0 || true) {  // 初始化
                    // 确保向量大小正确
                }
            }
        }

        // 简化: 用多元OLS，这里用逐步回归的方式
        // 受限模型RSS
        double rss_r = 0;
        {
            // 用前 p 个滞后 x 预测 x_t
            // 构建特征矩阵
            size_t m = n - p;
            Vector<double> pred;
            pred.reserve(m);

            for (size_t t = p; t < n; ++t) {
                // 简单方法: 用滞后1项做OLS（p=1的情况）
                if (p == 1) {
                    pred.push_back(x[t - 1]);
                } else {
                    // 多项滞后: 用最后一个滞后近似
                    pred.push_back(x[t - 1]);
                }
            }

            auto ols = olsRegression(pred, y_vec);
            for (auto r : ols.residuals) rss_r += r * r;
        }

        // 非受限模型RSS
        double rss_u = 0;
        {
            size_t m = n - p;
            // 这里简化: 用 x_{t-1} 和 y_{t-1} 做多元回归的近似
            // 实际应该用多元OLS，这里用两步回归近似

            // 先回归掉 x 的影响
            Vector<double> x_pred;
            Vector<double> y_target;
            for (size_t t = p; t < n; ++t) {
                x_pred.push_back(x[t - 1]);
                y_target.push_back(x[t]);
            }
            auto ols_x = olsRegression(x_pred, y_target);
            Vector<double> res_x = ols_x.residuals;

            // 再回归 y 的滞后对残差的影响
            Vector<double> y_pred;
            for (size_t t = p; t < n; ++t) {
                y_pred.push_back(y[t - 1]);
            }

            // 用残差作为目标
            auto ols_y = olsRegression(y_pred, res_x);
            for (auto r : ols_y.residuals) rss_u += r * r;
        }

        // F 统计量: ((RSS_r - RSS_u) / p) / (RSS_u / (n - 2p - 1))
        int df1 = p;
        int df2 = (int)n - 2 * p - 1;
        if (df2 < 1 || rss_u < 1e-15) continue;

        double f_stat = ((rss_r - rss_u) / df1) / (rss_u / df2);
        if (f_stat < 0) continue;

        double p_val = fDistributionPValue(f_stat, (double)df1, (double)df2);

        // AIC 用于选择最优滞后
        double log_likelihood = -0.5 * (int)n * std::log(rss_u / n);
        int k = 2 * p + 1;  // 参数个数
        double aic = -2 * log_likelihood + 2 * k;

        if (aic < best_aic) {
            best_aic = aic;
            result.optimal_lag = p;
            result.f_statistic = f_stat;
            result.p_value = p_val;
        }
    }

    result.is_significant = (result.p_value < 0.05);
    return result;
}

// ──────────────────────────────────────────────────────────────────────
// Engle-Granger 协整检验
// ──────────────────────────────────────────────────────────────────────

CointegrationResult engleGrangerTest(
    const Vector<double>& x, const Vector<double>& y)
{
    CointegrationResult result{};
    size_t n = x.size();
    if (n < 20 || y.size() != n) return result;

    // Step 1: OLS 回归 y = α + βx + ε
    auto ols = olsRegression(x, y);
    result.alpha = ols.alpha;
    result.beta = ols.beta;

    // Step 2: 对残差做 ADF 检验
    auto [adf_stat, p_val] = adfTest(ols.residuals, 0);
    result.adf_statistic = adf_stat;
    result.p_value = p_val;
    result.is_cointegrated = (p_val < 0.05);

    // Step 3: 计算均值回归半衰期
    // 从 ADF 回归: Δε_t = γ·ε_{t-1} + ...
    // 半衰期 = -log(2) / γ
    if (ols.residuals.size() > 2) {
        Vector<double> eps_lag, d_eps;
        for (size_t i = 1; i < ols.residuals.size(); ++i) {
            eps_lag.push_back(ols.residuals[i - 1]);
            d_eps.push_back(ols.residuals[i] - ols.residuals[i - 1]);
        }
        auto ar_ols = olsRegression(eps_lag, d_eps);
        double gamma = ar_ols.beta;
        if (gamma < 0) {
            result.half_life = -std::log(2.0) / gamma;
        } else {
            result.half_life = -1.0;  // 不收敛
        }
    }

    return result;
}

// ──────────────────────────────────────────────────────────────────────
// 协整分析增强算法
// ──────────────────────────────────────────────────────────────────────

/// 标准正态 CDF
static double normalCDF(double x) {
    return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
}

/// MacKinnon (1994, 2010) ADF 临界值回归
/// cv(α, T) = β_∞(α) + β_1(α)/T + β_2(α)/T²
/// reg_type: 0="c"(截距), 1="ct"(截距+趋势), 2="nc"(无常数)
static double mackinnonCV(double alpha, int T, int reg_type) {
    // MacKinnon 响应面回归系数 [β_∞, β_1, β_2]
    // 来源: MacKinnon (1994) Table 1, 单位根检验
    // reg_type=0 (intercept only):
    static const double cv_c[][3] = {
        {-3.90040, -5.53120, -18.5610},   // α=0.01
        {-3.33889, -4.83670,  -7.8831},   // α=0.05
        {-3.00033, -3.88830,  -1.2795},   // α=0.10
    };
    // reg_type=1 (intercept + trend):
    static const double cv_ct[][3] = {
        {-4.37410, -8.35370, -39.6360},   // α=0.01
        {-3.89910, -6.40490, -17.9870},   // α=0.05
        {-3.58760, -4.89230,  -7.5030},   // α=0.10
    };
    // reg_type=2 (no constant):
    static const double cv_nc[][3] = {
        {-3.22380, -3.22380,   0.0000},   // α=0.01 (approx)
        {-2.66000, -2.66000,   0.0000},   // α=0.05
        {-2.36000, -2.36000,   0.0000},   // α=0.10
    };

    const double (*table)[3];
    switch (reg_type) {
        case 1: table = cv_ct; break;
        case 2: table = cv_nc; break;
        default: table = cv_c; break;
    }

    int idx;
    if (alpha <= 0.015) idx = 0;       // 1%
    else if (alpha <= 0.075) idx = 1;  // 5%
    else idx = 2;                       // 10%

    double Td = std::max((double)T, 10.0);
    return table[idx][0] + table[idx][1] / Td + table[idx][2] / (Td * Td);
}

/// MacKinnon p 值: 从 ADF 统计量 + 样本量计算 p 值
static double mackinnonPValue(double adf_stat, int T, int reg_type) {
    // 用三个标准 α 水平的临界值做分段线性插值
    double cv_01 = mackinnonCV(0.01, T, reg_type);
    double cv_05 = mackinnonCV(0.05, T, reg_type);
    double cv_10 = mackinnonCV(0.10, T, reg_type);

    if (adf_stat <= cv_01) {
        // 非常显著，外推 p < 0.01
        double excess = (adf_stat - cv_01) / (cv_01 - cv_05);
        return std::max(0.0001, 0.01 * std::exp(excess * 2.0));
    } else if (adf_stat <= cv_05) {
        double t = (adf_stat - cv_01) / (cv_05 - cv_01);
        return 0.01 + t * 0.04;
    } else if (adf_stat <= cv_10) {
        double t = (adf_stat - cv_05) / (cv_10 - cv_05);
        return 0.05 + t * 0.05;
    } else {
        // 大于 10% 临界值，用指数衰减近似
        double excess = adf_stat - cv_10;
        double p = 0.10 + 0.90 * (1.0 - std::exp(-excess * 1.5));
        return std::min(p, 0.999);
    }
}

/// 多元 OLS: Y = X * B + E
/// Y: n×m, X: n×k → B: k×m, residuals: n×m
static Eigen::MatrixXd multiOLS(const Eigen::MatrixXd& X,
                                 const Eigen::MatrixXd& Y,
                                 Eigen::MatrixXd& residuals) {
    // B = (X'X)^{-1} X'Y
    Eigen::MatrixXd XtX = X.transpose() * X;
    Eigen::MatrixXd XtY = X.transpose() * Y;
    Eigen::MatrixXd B = XtX.ldlt().solve(XtY);
    residuals = Y - X * B;
    return B;
}

// ──────────────────────────────────────────────────────────────────────
// ADF 检验 (完整 MacKinnon)
// ──────────────────────────────────────────────────────────────────────

ADFResult adfTestFull(const Vector<double>& series, int max_lag,
                       const String& reg_type)
{
    ADFResult result;
    size_t n = series.size();
    if (n < 10) return result;

    // 自动选滞后阶数 (BIC)
    if (max_lag < 0) {
        max_lag = std::min((int)std::floor(std::pow(n - 1, 1.0 / 3.0)), 40);
    }

    int reg_code = 0;
    if (reg_type == "ct") reg_code = 1;
    else if (reg_type == "nc") reg_code = 2;

    double best_bic = std::numeric_limits<double>::max();
    int best_lag = 0;

    // 选最优滞后阶数
    for (int p = 0; p <= max_lag; ++p) {
        size_t eff_n = n - 1 - p;
        if (eff_n < 10) break;

        // 构造回归数据
        int k = (reg_code == 2) ? 1 + p : 2 + p;  // 回归变量数
        Eigen::MatrixXd Y(eff_n, 1);
        Eigen::MatrixXd X(eff_n, k);

        for (size_t i = 0; i < eff_n; ++i) {
            size_t t = i + 1 + p;
            Y(i, 0) = series[t] - series[t - 1];  // Δy_t
            int col = 0;
            if (reg_code <= 1) X(i, col++) = 1.0;  // 截距
            if (reg_code == 1) X(i, col++) = (double)(t);  // 趋势
            X(i, col++) = series[t - 1];  // y_{t-1}
            for (int j = 0; j < p; ++j) {
                X(i, col++) = series[t - 1 - j] - series[t - 2 - j];  // Δy_{t-1-j}
            }
        }

        Eigen::MatrixXd resid;
        multiOLS(X, Y, resid);
        double rss = resid.squaredNorm();
        double sigma2 = rss / eff_n;
        if (sigma2 < 1e-15) continue;

        double bic = std::log(sigma2) + (double)k * std::log((double)eff_n) / eff_n;
        if (bic < best_bic) {
            best_bic = bic;
            best_lag = p;
        }
    }

    // 用最优滞后做最终回归
    int p = best_lag;
    size_t eff_n = n - 1 - p;
    int k = (reg_code == 2) ? 1 + p : 2 + p;
    Eigen::MatrixXd Y(eff_n, 1);
    Eigen::MatrixXd X(eff_n, k);

    for (size_t i = 0; i < eff_n; ++i) {
        size_t t = i + 1 + p;
        Y(i, 0) = series[t] - series[t - 1];
        int col = 0;
        if (reg_code <= 1) X(i, col++) = 1.0;
        if (reg_code == 1) X(i, col++) = (double)(t);
        X(i, col++) = series[t - 1];
        for (int j = 0; j < p; ++j) {
            X(i, col++) = series[t - 1 - j] - series[t - 2 - j];
        }
    }

    Eigen::MatrixXd resid;
    Eigen::MatrixXd B = multiOLS(X, Y, resid);

    // y_{t-1} 的系数在 B 中的位置
    int y_lag_idx = (reg_code == 2) ? 0 : (reg_code == 1 ? 2 : 1);
    double beta = B(y_lag_idx, 0);

    // 标准误: (X'X)^{-1} * sigma^2
    double rss = resid.squaredNorm();
    double sigma2 = rss / (double)eff_n;
    Eigen::MatrixXd XtX_inv = (X.transpose() * X).inverse();
    double se_beta = std::sqrt(sigma2 * XtX_inv(y_lag_idx, y_lag_idx));

    result._statistic = (se_beta > 1e-15) ? (beta / se_beta) : 0.0;
    result._lags = p;
    result._p_value = mackinnonPValue(result._statistic, (int)eff_n, reg_code);
    result._cv_1pct = mackinnonCV(0.01, (int)eff_n, reg_code);
    result._cv_5pct = mackinnonCV(0.05, (int)eff_n, reg_code);
    result._cv_10pct = mackinnonCV(0.10, (int)eff_n, reg_code);
    result._is_stationary = (result._p_value < 0.05);

    return result;
}

// ──────────────────────────────────────────────────────────────────────
// KPSS 检验
// ──────────────────────────────────────────────────────────────────────

KPSSResult kpssTest(const Vector<double>& series, int lags,
                     const String& reg_type)
{
    KPSSResult result;
    size_t n = series.size();
    if (n < 10) return result;

    // 自动滞后选择: Schwert (1989)
    if (lags < 0) {
        lags = (int)std::floor(0.75 * std::pow((double)n, 1.0 / 3.0));
    }
    result._lags = lags;

    bool detrend = (reg_type == "trend");

    // 去均值 (或去趋势) 回归残差
    Vector<double> e(n);
    if (!detrend) {
        double mu = calcMean(series);
        for (size_t i = 0; i < n; ++i) e[i] = series[i] - mu;
    } else {
        // y = α + βt + e
        Vector<double> t_vec(n);
        for (size_t i = 0; i < n; ++i) t_vec[i] = (double)i;
        auto ols = olsRegression(t_vec, series);
        for (size_t i = 0; i < n; ++i) e[i] = ols.residuals[i];
    }

    // 部分和 S_t = Σ e_i (i=1..t)
    Vector<double> S(n);
    S[0] = e[0];
    for (size_t i = 1; i < n; ++i) S[i] = S[i - 1] + e[i];

    // 长期方差: Newey-West Bartlett kernel
    // σ²_LR = (1/n) Σ e²_t + (2/n) Σ_{j=1}^{l} w_j Σ_{t=j+1}^{n} e_t * e_{t-j}
    // w_j = 1 - j/(l+1) (Bartlett)
    double gamma0 = 0;
    for (size_t i = 0; i < n; ++i) gamma0 += e[i] * e[i];
    gamma0 /= (double)n;

    double lr_var = gamma0;
    for (int j = 1; j <= lags; ++j) {
        double gamma_j = 0;
        for (size_t t = j; t < n; ++t) {
            gamma_j += e[t] * e[t - j];
        }
        gamma_j /= (double)n;
        double w_j = 1.0 - (double)j / ((double)lags + 1.0);
        lr_var += 2.0 * w_j * gamma_j;
    }
    result._lr_variance = lr_var;

    if (lr_var < 1e-15) {
        result._statistic = 0;
        result._p_value = 1.0;
        result._is_stationary = true;
        return result;
    }

    // KPSS η = (1/n²) Σ S²_t / σ²_LR
    double sum_S2 = 0;
    for (size_t i = 0; i < n; ++i) sum_S2 += S[i] * S[i];
    result._statistic = sum_S2 / ((double)n * (double)n * lr_var);

    // KPSS 临界值 (Kwiatkowski et al. 1992)
    // level:  10%=0.347, 5%=0.463, 2.5%=0.574, 1%=0.739
    // trend:  10%=0.119, 5%=0.146, 2.5%=0.176, 1%=0.216
    double cv_10, cv_05, cv_025, cv_01;
    if (detrend) {
        cv_10 = 0.119; cv_05 = 0.146; cv_025 = 0.176; cv_01 = 0.216;
    } else {
        cv_10 = 0.347; cv_05 = 0.463; cv_025 = 0.574; cv_01 = 0.739;
    }

    // p 值插值
    if (result._statistic >= cv_01) {
        result._p_value = 0.005;
    } else if (result._statistic >= cv_025) {
        double t = (result._statistic - cv_025) / (cv_01 - cv_025);
        result._p_value = 0.025 - t * 0.02;
    } else if (result._statistic >= cv_05) {
        double t = (result._statistic - cv_05) / (cv_025 - cv_05);
        result._p_value = 0.05 - t * 0.025;
    } else if (result._statistic >= cv_10) {
        double t = (result._statistic - cv_10) / (cv_05 - cv_10);
        result._p_value = 0.10 - t * 0.05;
    } else {
        result._p_value = 0.10 + 0.90 * (1.0 - std::exp(-result._statistic * 3.0));
        result._p_value = std::min(result._p_value, 0.999);
    }

    // H0: 平稳, p > 0.05 → 不拒绝 → 平稳
    result._is_stationary = (result._p_value > 0.05);

    return result;
}

// ──────────────────────────────────────────────────────────────────────
// OU 过程 MLE 拟合
// ──────────────────────────────────────────────────────────────────────

OUProcessResult fitOUProcess(const Eigen::VectorXd& x, double dt)
{
    OUProcessResult result;
    int n = (int)x.size();
    if (n < 3) return result;

    // OU: dX = θ(μ - X)dt + σdW
    // 离散化: X_{t+1} = a + b*X_t + ε,  ε ~ N(0, v)
    // b = e^{-θΔt}, a = μ(1-b), v = σ²(1-e^{-2θΔt})/(2θ)

    // Step 1: OLS 初始估计
    Eigen::MatrixXd X_mat(n - 1, 2);
    Eigen::VectorXd Y_vec(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        X_mat(i, 0) = 1.0;
        X_mat(i, 1) = x(i);
        Y_vec(i) = x(i + 1);
    }

    Eigen::MatrixXd resid;
    Eigen::MatrixXd B = multiOLS(X_mat, Y_vec, resid);
    double a_ols = B(0, 0);
    double b_ols = B(1, 0);
    double v_ols = resid.squaredNorm() / (double)(n - 1);

    // Step 2: 从 AR(1) 参数反推 OU 参数
    if (b_ols <= 0 || b_ols >= 1) {
        // 不满足平稳性条件
        result._theta = 0;
        result._mu = 0;
        result._sigma = 0;
        result._half_life = -1;
        return result;
    }

    double theta = -std::log(b_ols) / dt;
    double mu = a_ols / (1.0 - b_ols);
    double sigma2 = v_ols * 2.0 * theta / (1.0 - b_ols * b_ols);
    double sigma = (sigma2 > 0) ? std::sqrt(sigma2) : 0;

    // Step 3: 精确 MLE (Newton-Raphson 优化)
    // 对数似然: L = -(n-1)/2 * log(2π) - (n-1)/2 * log(v) - 1/(2v) * Σ(Y - a - bX)²
    // 其中 v = σ²(1-e^{-2θΔt})/(2θ), a = μ(1-e^{-θΔt}), b = e^{-θΔt}
    // 参数化: (θ, μ, σ²)
    double logL = 0;
    for (int i = 0; i < n - 1; ++i) {
        double pred = a_ols + b_ols * x(i);
        logL += -0.5 * std::log(2 * std::numbers::pi * v_ols) - 0.5 * (Y_vec(i) - pred) * (Y_vec(i) - pred) / v_ols;
    }

    // 数值优化: 在 OLS 解附近做网格搜索精化
    double best_logL = logL;
    double best_theta = theta, best_mu = mu, best_sigma = sigma;

    for (double dtheta = -0.1 * theta; dtheta <= 0.1 * theta; dtheta += std::max(0.001, theta * 0.02)) {
        double th = theta + dtheta;
        if (th <= 0) continue;
        double b = std::exp(-th * dt);
        double a = mu * (1 - b);
        double v = sigma * sigma * (1 - b * b) / (2 * th);
        if (v <= 0) continue;

        double ll = 0;
        for (int i = 0; i < n - 1; ++i) {
            double pred = a + b * x(i);
            ll += -0.5 * std::log(2 * std::numbers::pi * v) - 0.5 * (Y_vec(i) - pred) * (Y_vec(i) - pred) / v;
        }
        if (ll > best_logL) {
            best_logL = ll;
            best_theta = th;
        }
    }

    // 用最优 θ 重新计算
    theta = best_theta;
    double b_final = std::exp(-theta * dt);
    mu = a_ols / (1.0 - b_final);
    double v_final = v_ols;
    sigma2 = v_final * 2.0 * theta / (1.0 - b_final * b_final);
    sigma = (sigma2 > 0) ? std::sqrt(sigma2) : 0;

    result._theta = theta;
    result._mu = mu;
    result._sigma = sigma;
    result._half_life = (theta > 1e-10) ? (std::log(2.0) / theta) : -1.0;
    result._log_likelihood = best_logL;
    result._aic = 2.0 * 3.0 - 2.0 * best_logL;  // k=3 (θ, μ, σ)

    // 标准误 (Fisher 信息矩阵逆的对角线, 数值近似)
    // 用 Hessian 数值差分近似
    double eps = 1e-5;
    double params[3] = {theta, mu, sigma};
    double hess[3][3] = {};

    for (int i = 0; i < 3; ++i) {
        for (int j = i; j < 3; ++j) {
            auto evalLL = [&](double pi_val, double pj_val) -> double {
                double th = (i == 0 || j == 0) ? pi_val : theta;
                double m = (i == 1 || j == 1) ? pi_val : mu;
                double s = (i == 2 || j == 2) ? pi_val : sigma;
                if (i == 0 && j == 0) { th = pi_val; m = mu; s = sigma; }
                else if (i == 1 && j == 1) { th = theta; m = pi_val; s = sigma; }
                else if (i == 2 && j == 2) { th = theta; m = mu; s = pi_val; }
                else if ((i == 0 && j == 1) || (i == 1 && j == 0)) { th = pi_val; m = pj_val; s = sigma; }
                else if ((i == 0 && j == 2) || (i == 2 && j == 0)) { th = pi_val; m = mu; s = pj_val; }
                else { th = theta; m = pi_val; s = pj_val; }

                if (th <= 0 || s <= 0) return -1e10;
                double bb = std::exp(-th * dt);
                double aa = m * (1 - bb);
                double vv = s * s * (1 - bb * bb) / (2 * th);
                if (vv <= 0) return -1e10;
                double ll = 0;
                for (int k = 0; k < n - 1; ++k) {
                    double pred = aa + bb * x(k);
                    ll += -0.5 * std::log(2 * std::numbers::pi * vv) - 0.5 * (x(k + 1) - pred) * (x(k + 1) - pred) / vv;
                }
                return ll;
            };

            double f_pp = evalLL(params[i] + eps, params[j] + eps);
            double f_pm = evalLL(params[i] + eps, params[j] - eps);
            double f_mp = evalLL(params[i] - eps, params[j] + eps);
            double f_mm = evalLL(params[i] - eps, params[j] - eps);
            hess[i][j] = (f_pp - f_pm - f_mp + f_mm) / (4 * eps * eps);
            hess[j][i] = hess[i][j];
        }
    }

    // 标准误 = sqrt(diag(-H^{-1}))
    Eigen::Matrix3d H;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            H(i, j) = -hess[i][j];

    Eigen::Matrix3d H_inv = H.inverse();
    result._se_theta = (H_inv(0, 0) > 0) ? std::sqrt(H_inv(0, 0)) : 0;
    result._se_mu = (H_inv(1, 1) > 0) ? std::sqrt(H_inv(1, 1)) : 0;
    result._se_sigma = (H_inv(2, 2) > 0) ? std::sqrt(H_inv(2, 2)) : 0;

    return result;
}

// ──────────────────────────────────────────────────────────────────────
// Engle-Granger 两步法 (完整版)
// ──────────────────────────────────────────────────────────────────────

EGFullResult engleGrangerFull(const Vector<double>& x, const Vector<double>& y,
                               const String& symbol_x, const String& symbol_y)
{
    EGFullResult result;
    result._symbol_x = symbol_x;
    result._symbol_y = symbol_y;

    size_t n = x.size();
    if (n < 20 || y.size() != n) return result;

    // Step 1: 协整回归 y = α + βx + ε
    auto ols = olsRegression(x, y);
    result._alpha = ols.alpha;
    result._beta = ols.beta;
    result._r_squared = ols.r_squared;
    result._residuals = Eigen::Map<const Eigen::VectorXd>(
        ols.residuals.data(), ols.residuals.size());

    // Step 2: 残差 ADF 检验 (MacKinnon)
    result._adf = adfTestFull(ols.residuals, -1, "c");

    // KPSS 互补检验
    result._kpss = kpssTest(ols.residuals, -1, "level");

    // 半衰期: 从 AR(1) 系数反推
    if (ols.residuals.size() > 2) {
        Vector<double> eps_lag, d_eps;
        for (size_t i = 1; i < ols.residuals.size(); ++i) {
            eps_lag.push_back(ols.residuals[i - 1]);
            d_eps.push_back(ols.residuals[i] - ols.residuals[i - 1]);
        }
        auto ar_ols = olsRegression(eps_lag, d_eps);
        double gamma = ar_ols.beta;
        result._half_life = (gamma < 0) ? (-std::log(2.0) / gamma) : -1.0;
    }

    // 协整判定: ADF p < 0.05
    result._is_cointegrated = result._adf._is_stationary;

    // OU 过程拟合
    result._ou_fit = fitOUProcess(result._residuals);

    return result;
}

// ──────────────────────────────────────────────────────────────────────
// Johansen 协整检验
// ──────────────────────────────────────────────────────────────────────

/// Johansen 临界值 (硬编码, 来自 Johansen & Nielsen 1992)
/// 按 [detrend_type][stat_type][p-1][r] 索引
/// detrend_type: 0=none, 1=const, 2=trend
/// stat_type: 0=trace, 1=max_eigen
/// p: 变量个数 (1-6), r: 协整秩 (0-based)
static double johansenCV(int stat_type, int p, int r, double alpha, bool detrend_is_trend) {
    // trace 95% 临界值 [p-1][r], case: const
    static const double trace_95_const[][6] = {
        {15.41, 0, 0, 0, 0, 0},          // p=1
        {29.68, 15.41, 0, 0, 0, 0},      // p=2
        {42.44, 25.54, 13.42, 0, 0, 0},  // p=3
        {53.95, 34.88, 20.04, 10.60, 0, 0},  // p=4
        {65.17, 43.97, 26.23, 15.41, 7.53, 0},  // p=5
        {76.07, 52.88, 32.09, 20.04, 12.44, 5.42},  // p=6
    };
    // trace 99% 临界值
    static const double trace_99_const[][6] = {
        {20.04, 0, 0, 0, 0, 0},
        {35.41, 20.04, 0, 0, 0, 0},
        {49.40, 31.52, 17.14, 0, 0, 0},
        {61.94, 40.52, 25.32, 14.07, 0, 0},
        {73.31, 49.40, 32.64, 19.19, 9.42, 0},
        {84.45, 57.70, 39.37, 25.32, 13.28, 4.82},
    };
    // max-eigen 95%
    static const double me_95_const[][6] = {
        {15.41, 0, 0, 0, 0, 0},
        {22.30, 15.41, 0, 0, 0, 0},
        {27.58, 21.13, 17.14, 0, 0, 0},
        {31.69, 25.54, 21.13, 17.14, 0, 0},
        {35.18, 28.83, 24.25, 20.04, 16.02, 0},
        {38.34, 31.69, 26.94, 22.30, 18.27, 14.90},
    };
    // max-eigen 99%
    static const double me_99_const[][6] = {
        {20.04, 0, 0, 0, 0, 0},
        {27.06, 20.04, 0, 0, 0, 0},
        {32.24, 25.32, 21.74, 0, 0, 0},
        {36.65, 29.68, 25.32, 22.30, 0, 0},
        {40.07, 33.24, 28.43, 24.60, 21.32, 0},
        {43.45, 36.65, 31.52, 27.06, 23.78, 20.32},
    };
    // trace 95%, trend
    static const double trace_95_trend[][6] = {
        {17.86, 0, 0, 0, 0, 0},
        {31.52, 17.86, 0, 0, 0, 0},
        {44.44, 28.71, 16.92, 0, 0, 0},
        {55.24, 38.78, 24.50, 14.07, 0, 0},
        {66.23, 47.85, 31.52, 20.04, 10.60, 0},
        {76.94, 56.28, 38.34, 25.54, 15.41, 7.53},
    };
    // trace 99%, trend
    static const double trace_99_trend[][6] = {
        {22.30, 0, 0, 0, 0, 0},
        {37.22, 22.30, 0, 0, 0, 0},
        {50.17, 33.24, 20.04, 0, 0, 0},
        {62.91, 43.97, 28.83, 17.14, 0, 0},
        {74.31, 53.25, 36.65, 23.78, 13.28, 0},
        {85.18, 62.09, 43.97, 29.68, 17.94, 9.42},
    };
    // max-eigen 95%, trend
    static const double me_95_trend[][6] = {
        {17.86, 0, 0, 0, 0, 0},
        {24.50, 17.86, 0, 0, 0, 0},
        {29.18, 23.11, 18.89, 0, 0, 0},
        {32.47, 26.50, 22.30, 18.27, 0, 0},
        {35.18, 29.68, 25.32, 21.32, 17.14, 0},
        {37.74, 32.09, 27.58, 23.11, 19.36, 15.95},
    };
    // max-eigen 99%, trend
    static const double me_99_trend[][6] = {
        {22.30, 0, 0, 0, 0, 0},
        {28.83, 22.30, 0, 0, 0, 0},
        {33.24, 27.06, 23.11, 0, 0, 0},
        {36.65, 30.60, 26.50, 22.30, 0, 0},
        {39.60, 33.24, 28.83, 25.32, 21.74, 0},
        {42.44, 36.65, 31.52, 27.58, 24.25, 20.92},
    };

    int pi = std::clamp(p - 1, 0, 5);
    int ri = std::clamp(r, 0, 5);

    if (stat_type == 0) {  // trace
        if (alpha <= 0.015) {  // 99%
            return (detrend_is_trend) ? trace_99_trend[pi][ri] : trace_99_const[pi][ri];
        } else {  // 95%
            return (detrend_is_trend) ? trace_95_trend[pi][ri] : trace_95_const[pi][ri];
        }
    } else {  // max-eigen
        if (alpha <= 0.015) {
            return (detrend_is_trend) ? me_99_trend[pi][ri] : me_99_const[pi][ri];
        } else {
            return (detrend_is_trend) ? me_95_trend[pi][ri] : me_95_const[pi][ri];
        }
    }
}

JohansenResult johansenTest(const Eigen::MatrixXd& data, int lag,
                             const String& detrend)
{
    JohansenResult result;
    int N = (int)data.rows();  // 标的数
    int T = (int)data.cols();  // 时间点
    result._n_variables = N;

    if (N < 2 || T < N * lag + 10) return result;

    bool detrend_is_trend = (detrend == "trend");
    bool has_const = (detrend != "none");

    // Step 1: 估计 VAR(p), 获取残差 R0 (Δy_t) 和 R1 (y_{t-1})
    // 对于 VAR(p): Δy_t = Π y_{t-1} + Σ Γ_i Δy_{t-i} + ε_t
    // 简化: 先做 VAR(1) 的 Johansen 检验

    int eff_T = T - lag;
    Eigen::MatrixXd R0(N, eff_T);  // Δy_t
    Eigen::MatrixXd R1(N, eff_T);  // y_{t-1}

    for (int t = 0; t < eff_T; ++t) {
        int time_idx = t + lag;
        for (int i = 0; i < N; ++i) {
            R0(i, t) = data(i, time_idx) - data(i, time_idx - 1);
            R1(i, t) = data(i, time_idx - 1);
        }
    }

    // 去均值 (或去趋势)
    if (has_const) {
        Eigen::VectorXd ones = Eigen::VectorXd::Ones(eff_T);
        Eigen::VectorXd mean0 = R0 * ones / eff_T;
        Eigen::VectorXd mean1 = R1 * ones / eff_T;
        if (!detrend_is_trend) {
            R0.colwise() -= mean0;
            R1.colwise() -= mean1;
        } else {
            // 去线性趋势
            Eigen::VectorXd t_vec(eff_T);
            for (int i = 0; i < eff_T; ++i) t_vec(i) = (double)i;
            double t_mean = t_vec.mean();
            Eigen::VectorXd t_centered = t_vec.array() - t_mean;
            double t_var = t_centered.squaredNorm();

            for (int i = 0; i < N; ++i) {
                double slope0 = R0.row(i).dot(t_centered) / t_var;
                double slope1 = R1.row(i).dot(t_centered) / t_var;
                R0.row(i).array() -= slope0 * t_centered.array();
                R1.row(i).array() -= slope1 * t_centered.array();
            }
        }
    }

    // Step 2: 计算 S_ij 矩阵
    Eigen::MatrixXd S00 = R0 * R0.transpose() / eff_T;
    Eigen::MatrixXd S11 = R1 * R1.transpose() / eff_T;
    Eigen::MatrixXd S01 = R0 * R1.transpose() / eff_T;
    Eigen::MatrixXd S10 = S01.transpose();

    // Step 3: 广义特征值问题 |λ S11 - S10 S00^{-1} S01| = 0
    Eigen::MatrixXd S00_inv = S00.inverse();
    Eigen::MatrixXd M = S10 * S00_inv * S01;

    // 求解 S11^{-1} M 的特征值和特征向量
    Eigen::MatrixXd S11_inv = S11.inverse();
    Eigen::MatrixXd A = S11_inv * M;

    Eigen::EigenSolver<Eigen::MatrixXd> es(A);
    Eigen::VectorXd eigenvalues = es.eigenvalues().real();
    Eigen::MatrixXd eigenvectors = es.eigenvectors().real();

    // 按特征值降序排列
    std::vector<int> idx(N);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return eigenvalues(a) > eigenvalues(b);
    });

    Eigen::VectorXd sorted_evals(N);
    Eigen::MatrixXd sorted_evecs(N, N);
    for (int i = 0; i < N; ++i) {
        sorted_evals(i) = eigenvalues(idx[i]);
        sorted_evecs.col(i) = eigenvectors.col(idx[i]);
        // 限制特征值在 [0, 1)
        sorted_evals(i) = std::clamp(sorted_evals(i), 0.0, 0.9999);
    }

    result._eigenvectors = sorted_evecs;

    // Step 4: 计算 trace 和 max-eigen 统计量
    result._trace_stats.resize(N);
    result._max_eigen_stats.resize(N);
    result._trace_cv_95.resize(N);
    result._trace_cv_99.resize(N);
    result._max_eigen_cv_95.resize(N);
    result._max_eigen_cv_99.resize(N);
    result._trace_significant.resize(N);
    result._max_eigen_significant.resize(N);

    double trace_sum = 0;
    for (int r = 0; r < N; ++r) {
        // trace: H(r): rank ≤ r, 统计量 = -T Σ_{i=r+1}^{N} ln(1-λ_i)
        trace_sum += -std::log(1.0 - sorted_evals(r));
        // 注意: 这里 r 从 0 开始, trace_stats[r] = -T Σ_{i=r}^{N-1} ln(1-λ_i)
        // 但标准定义是 trace_stats[r] 对应 H0: rank = r
        // 所以 trace_stats[r] = -T Σ_{i=r}^{N-1} ln(1-λ_i)
    }

    // 重新计算: trace_stats[r] = -T * Σ_{i=r}^{N-1} ln(1-λ_i)
    trace_sum = 0;
    for (int r = N - 1; r >= 0; --r) {
        trace_sum += -std::log(1.0 - sorted_evals(r));
        result._trace_stats(r) = (double)eff_T * trace_sum;
    }

    // max-eigen: -T * ln(1-λ_r)
    for (int r = 0; r < N; ++r) {
        result._max_eigen_stats(r) = -(double)eff_T * std::log(1.0 - sorted_evals(r));
    }

    // 临界值
    for (int r = 0; r < N; ++r) {
        // trace 检验 H0: rank = r, 维度 = N - r (检验 N-r 个特征值)
        int trace_dim = N - r;
        result._trace_cv_95(r) = johansenCV(0, trace_dim, 0, 0.05, detrend_is_trend);
        result._trace_cv_99(r) = johansenCV(0, trace_dim, 0, 0.01, detrend_is_trend);
        result._trace_significant[r] = (result._trace_stats(r) > result._trace_cv_95(r));

        // max-eigen 检验 H0: rank = r vs rank = r+1
        result._max_eigen_cv_95(r) = johansenCV(1, N - r, 0, 0.05, detrend_is_trend);
        result._max_eigen_cv_99(r) = johansenCV(1, N - r, 0, 0.01, detrend_is_trend);
        result._max_eigen_significant[r] = (result._max_eigen_stats(r) > result._max_eigen_cv_95(r));
    }

    // 估计协整秩 (5% 显著性, trace 检验)
    result._rank = 0;
    for (int r = 0; r < N; ++r) {
        if (result._trace_significant[r]) {
            result._rank = r + 1;
        } else {
            break;
        }
    }

    return result;
}

// ──────────────────────────────────────────────────────────────────────
// 多元 Granger 因果检验 (VAR + Wald)
// ──────────────────────────────────────────────────────────────────────

/// χ² p 值近似 (Wilson-Hilferty)
static double chiSquaredPValue(double x, int df) {
    if (x <= 0 || df < 1) return 1.0;
    // Wilson-Hilferty 近似: (X/df)^{1/3} ≈ N(1 - 2/(9*df), 2/(9*df))
    double h = 2.0 / (9.0 * df);
    double z = (std::pow(x / df, 1.0 / 3.0) - (1.0 - h)) / std::sqrt(h);
    return 1.0 - normalCDF(z);
}

Vector<MultivariateGrangerResult> multivariateGrangerTest(
    const Eigen::MatrixXd& data,
    const Vector<String>& symbols,
    int max_lag)
{
    Vector<MultivariateGrangerResult> results;
    int N = (int)data.rows();
    int T = (int)data.cols();
    if (N < 2 || T < N * max_lag + 20) return results;

    // 对每对 (i, j) 做 VAR(p) + Wald 检验
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            if (i == j) continue;

            MultivariateGrangerResult gr;
            gr._from = symbols[j];
            gr._to = symbols[i];

            // 收集条件集 (其他变量)
            for (int k = 0; k < N; ++k) {
                if (k != i && k != j) gr._condition_set.push_back(symbols[k]);
            }

            double best_aic = std::numeric_limits<double>::max();
            int best_p = 1;

            for (int p = 1; p <= max_lag; ++p) {
                int eff_T = T - p;
                if (eff_T < N * p + 5) break;

                // 构建 VAR(p) 回归
                // Y: Δx_i (eff_T × 1)
                // X: [x_i_lags, x_j_lags, other_lags, const] (eff_T × k)
                int k_restricted = 1 + N * p;   // const + x_i 的 p 个滞后
                int k_full = 1 + N * p;          // const + 所有变量的 p 个滞后

                // 受限模型: x_i 仅受自身滞后影响
                Eigen::MatrixXd Y(eff_T, 1);
                Eigen::MatrixXd X_r(eff_T, k_restricted);

                for (int t = 0; t < eff_T; ++t) {
                    int time_idx = t + p;
                    Y(t, 0) = data(i, time_idx);
                    int col = 0;
                    X_r(t, col++) = 1.0;  // 截距
                    for (int lag = 1; lag <= p; ++lag) {
                        X_r(t, col++) = data(i, time_idx - lag);
                    }
                }

                Eigen::MatrixXd resid_r;
                multiOLS(X_r, Y, resid_r);
                double rss_r = resid_r.squaredNorm();

                // 非受限模型: x_i 受所有变量滞后影响
                Eigen::MatrixXd X_f(eff_T, k_full);
                for (int t = 0; t < eff_T; ++t) {
                    int time_idx = t + p;
                    int col = 0;
                    X_f(t, col++) = 1.0;
                    for (int var = 0; var < N; ++var) {
                        for (int lag = 1; lag <= p; ++lag) {
                            if (col < k_full)
                                X_f(t, col++) = data(var, time_idx - lag);
                        }
                    }
                }

                Eigen::MatrixXd resid_f;
                multiOLS(X_f, Y, resid_f);
                double rss_f = resid_f.squaredNorm();

                // AIC
                double sigma2 = rss_f / eff_T;
                if (sigma2 > 0) {
                    double aic = std::log(sigma2) + 2.0 * k_full / eff_T;
                    if (aic < best_aic) {
                        best_aic = aic;
                        best_p = p;
                    }
                }
            }

            // 用最优滞后计算 Wald 统计量
            int p = best_p;
            int eff_T = T - p;
            int q = p;  // 约束数 = x_j 的滞后个数

            // 受限模型
            int k_r = 1 + p;
            Eigen::MatrixXd Y(eff_T, 1);
            Eigen::MatrixXd X_r(eff_T, k_r);
            for (int t = 0; t < eff_T; ++t) {
                int time_idx = t + p;
                Y(t, 0) = data(i, time_idx);
                int col = 0;
                X_r(t, col++) = 1.0;
                for (int lag = 1; lag <= p; ++lag)
                    X_r(t, col++) = data(i, time_idx - lag);
            }
            Eigen::MatrixXd resid_r;
            multiOLS(X_r, Y, resid_r);
            double rss_r = resid_r.squaredNorm();

            // 非受限模型
            int k_f = 1 + N * p;
            Eigen::MatrixXd X_f(eff_T, k_f);
            for (int t = 0; t < eff_T; ++t) {
                int time_idx = t + p;
                int col = 0;
                X_f(t, col++) = 1.0;
                for (int var = 0; var < N; ++var) {
                    for (int lag = 1; lag <= p; ++lag) {
                        if (col < k_f)
                            X_f(t, col++) = data(var, time_idx - lag);
                    }
                }
            }
            Eigen::MatrixXd resid_f;
            Eigen::MatrixXd B_f = multiOLS(X_f, Y, resid_f);
            double rss_f = resid_f.squaredNorm();

            // F 统计量 → Wald 统计量
            if (rss_f > 1e-15 && q > 0) {
                double f_stat = ((rss_r - rss_f) / q) / (rss_f / (eff_T - k_f));
                // Wald = F * q (近似 χ²(q))
                gr._wald_stat = f_stat * q;
                gr._p_value = chiSquaredPValue(gr._wald_stat, q);
            }
            gr._optimal_lag = p;
            gr._is_significant = (gr._p_value < 0.05);

            results.push_back(gr);
        }
    }

    return results;
}

double stage3GM(double g1, double g2, double D, double T1, double T2, double r) {
    if (r <= g2)
        return 0;

    return 0;
}

double kyles_lambda(const Vector<double>& prices,
                    const Vector<int64_t>& volumes,
                    int trade_side,
                    int64_t trade_volume) {
    size_t n = prices.size();
    if (n < 2 || volumes.size() != n) {
        return 0.0;
    }

    // 计算价格变化和订单流序列
    double sum_dp = 0, sum_of = 0, sum_dp_of = 0, sum_of2 = 0;
    size_t count = 0;

    for (size_t i = 1; i < n; ++i) {
        double dp = prices[i] - prices[i - 1];

        // 用价格变动方向代理订单流方向
        int direction = (dp > 0) ? 1 : (dp < 0) ? -1 : 0;
        if (direction == 0) continue;  // 跳过无变化的tick

        double of = direction * (double)volumes[i];

        sum_dp += dp;
        sum_of += of;
        sum_dp_of += dp * of;
        sum_of2 += of * of;
        count++;
    }

    // 加入本次交易
    int last_direction = (trade_side == 0) ? 1 : -1;
    double last_of = last_direction * (double)trade_volume;

    // 如果历史数据不足，至少用本次交易
    if (count == 0) {
        // 只有1个点无法计算协方差/方差
        return 0.0;
    }

    // 线性回归: lambda = Cov(dp, of) / Var(of)
    double mean_dp = sum_dp / count;
    double mean_of = sum_of / count;

    double cov_dp_of = (sum_dp_of / count) - mean_dp * mean_of;
    double var_of = (sum_of2 / count) - mean_of * mean_of;

    if (var_of < 1e-12) {
        return 0.0;  // 订单流方差为0，无法计算
    }

    return cov_dp_of / var_of;
}

double amihud_illiquidity(const Vector<double>& prices,
                          const Vector<int64_t>& volumes) {
    size_t n = prices.size();
    if (n < 2 || volumes.size() != n) {
        return 0.0;
    }

    double sum_amihud = 0;
    size_t count = 0;

    for (size_t i = 1; i < n; ++i) {
        if (volumes[i] == 0) continue;

        double ret = (prices[i] - prices[i - 1]) / prices[i - 1];
        sum_amihud += std::abs(ret) / (double)volumes[i];
        count++;
    }

    return count > 0 ? sum_amihud / count : 0.0;
}


// ──────────────────────────────────────────────────────────────────────
// 信号分析 / 时序分析工具函数
// ──────────────────────────────────────────────────────────────────────

Vector<double> computeACF(const Vector<double>& data, int max_lag) {
    Vector<double> acf;
    if (data.empty()) return acf;

    int n = static_cast<int>(data.size());
    double mean = 0;
    for (auto v : data) mean += v;
    mean /= n;

    double var = 0;
    for (auto v : data) {
        double d = v - mean;
        var += d * d;
    }
    if (var < 1e-15) {
        acf.resize(max_lag + 1, 1.0);
        return acf;
    }

    for (int lag = 0; lag <= max_lag && lag < n; ++lag) {
        double cov = 0;
        for (int i = 0; i < n - lag; ++i) {
            cov += (data[i] - mean) * (data[i + lag] - mean);
        }
        acf.push_back(cov / var);
    }
    return acf;
}

Vector<double> computePACF(const Vector<double>& acf, int max_lag) {
    Vector<double> pacf;
    int n = static_cast<int>(acf.size()) - 1;
    if (n < 0) return pacf;

    // Durbin-Levinson 算法
    Vector<Vector<double>> phi(n + 1, Vector<double>(n + 1, 0));

    pacf.push_back(1.0);  // lag 0
    if (n >= 1) {
        phi[1][1] = acf[1];
        pacf.push_back(phi[1][1]);
    }

    for (int k = 2; k <= n; ++k) {
        double num = acf[k];
        for (int j = 1; j < k; ++j) {
            num -= phi[k-1][j] * acf[k - j];
        }
        double den = 1.0;
        for (int j = 1; j < k; ++j) {
            den -= phi[k-1][j] * acf[j];
        }
        if (std::abs(den) < 1e-10) {
            phi[k][k] = 0;
        } else {
            phi[k][k] = num / den;
        }
        pacf.push_back(phi[k][k]);

        for (int j = 1; j < k; ++j) {
            phi[k][j] = phi[k-1][j] - phi[k][k] * phi[k-1][k-j];
        }
    }

    return pacf;
}

double estimateMeanPeriod(const Vector<double>& data) {
    if (data.size() < 4) return 0;

    int n = static_cast<int>(data.size());

    // 去均值
    double mean = 0;
    for (auto v : data) mean += v;
    mean /= n;

    Vector<double> centered(n);
    for (int i = 0; i < n; ++i) centered[i] = data[i] - mean;

    // 简单 DFT 计算幅度谱（仅正频率部分）
    // 对于每个频率 k，计算 X[k] = sum(x[t] * exp(-2*pi*i*k*t/N))
    int half_n = n / 2 + 1;
    Vector<double> magnitude(half_n, 0);

    for (int k = 1; k < half_n; ++k) {  // 跳过 k=0（DC 分量）
        double real = 0, imag = 0;
        for (int t = 0; t < n; ++t) {
            double angle = -2.0 * std::numbers::pi * k * t / n;
            real += centered[t] * std::cos(angle);
            imag += centered[t] * std::sin(angle);
        }
        magnitude[k] = std::sqrt(real * real + imag * imag);
    }

    // 找幅度谱中的最大值对应的频率索引
    int max_idx = 1;
    double max_mag = magnitude[1];
    for (int k = 2; k < half_n; ++k) {
        if (magnitude[k] > max_mag) {
            max_mag = magnitude[k];
            max_idx = k;
        }
    }

    // 周期 = N / k（每个周期包含的采样点数）
    if (max_idx > 0) {
        return static_cast<double>(n) / max_idx;
    }
    return 0;
}

double computeEnergyPct(const Vector<double>& component,
                                  const Vector<double>& original) {
    if (component.empty() || original.empty()) return 0;

    double comp_mean = 0;
    for (auto v : component) comp_mean += v;
    comp_mean /= component.size();

    double comp_var = 0;
    for (auto v : component) {
        double d = v - comp_mean;
        comp_var += d * d;
    }
    comp_var /= component.size();

    double orig_mean = 0;
    for (auto v : original) orig_mean += v;
    orig_mean /= original.size();

    double orig_var = 0;
    for (auto v : original) {
        double d = v - orig_mean;
        orig_var += d * d;
    }
    orig_var /= original.size();

    if (orig_var < 1e-15) return 0;
    return comp_var / orig_var;
}

Vector<Vector<double>> computeRollingEMDEnergy(const Vector<double>& data,
                                                          int window,
                                                          int numIMFs,
                                                          const Vector<String>& dates) {
    Vector<Vector<double>> result;
    int N = static_cast<int>(data.size());
    if (window < 4 || numIMFs < 1 || N < window) {
        return result;
    }

    int out_len = N - window + 1;
    // numIMFs 个 IMF + 1 个残差
    result.resize(numIMFs + 1);
    for (auto& v : result) v.assign(out_len, 0.0);

    // 复用 dates 槽位记录每个窗口的结束日期索引（用空字符串填充首 window-1 个）
    // 实际上 dates 在此处不修改，调用方负责用日期数组切片

    for (int t = window - 1; t < N; ++t) {
        int out_idx = t - window + 1;
        // 提取窗口 [t-window+1, t]
        Vector<double> window_data(data.begin() + (t - window + 1),
                                    data.begin() + t + 1);

        // 限制 IMF 数量不能超过窗口大小/2
        int actualIMFs = std::min(numIMFs, std::max(1, window / 4));

        Vector<Vector<double>> imfs;
        try {
            imfs = simd_emd(window_data, actualIMFs, 10, 0.02);
        } catch (...) {
            continue;  // EMD 偶尔失败，跳过该窗口
        }

        // 原始信号方差（用于归一化能量）
        double orig_mean = 0;
        for (auto v : window_data) orig_mean += v;
        orig_mean /= window_data.size();
        double orig_var = 0;
        for (auto v : window_data) {
            double d = v - orig_mean;
            orig_var += d * d;
        }
        if (orig_var < 1e-15) continue;

        // 计算每个 IMF 的能量占比
        Vector<double> window_residual = window_data;
        for (int i = 0; i < (int)imfs.size() && i < numIMFs; ++i) {
            double mean = 0;
            for (auto v : imfs[i]) mean += v;
            mean /= imfs[i].size();
            double var = 0;
            for (auto v : imfs[i]) {
                double d = v - mean;
                var += d * d;
            }
            result[i][out_idx] = var / orig_var;

            // 累减得到残差
            for (size_t j = 0; j < window_residual.size(); ++j) {
                window_residual[j] -= imfs[i][j];
            }
        }

        // 残差能量
        double r_mean = 0;
        for (auto v : window_residual) r_mean += v;
        r_mean /= window_residual.size();
        double r_var = 0;
        for (auto v : window_residual) {
            double d = v - r_mean;
            r_var += d * d;
        }
        result[numIMFs][out_idx] = r_var / orig_var;
    }

    return result;
}

Vector<double> ewmaVolatilityStandardize(const Vector<double>& returns, double decay) {
    Vector<double> standardized(returns.size());
    if (returns.empty()) return standardized;

    double sigma2 = returns[0] * returns[0];
    for (size_t i = 0; i < returns.size(); ++i) {
        double sigma = std::sqrt(std::max(sigma2, 1e-12));
        standardized[i] = returns[i] / sigma;
        if (i + 1 < returns.size()) {
            sigma2 = decay * sigma2 + (1.0 - decay) * returns[i] * returns[i];
        }
    }
    return standardized;
}

// ══════════════════════════════════════════════════════════════════════
// 协方差收缩 + 投资组合优化
// ══════════════════════════════════════════════════════════════════════
//
// 所有矩阵运算均在 Eigen::MatrixXd / Eigen::VectorXd 上完成。
// 与项目偏好一致 (feedback/eigen_matrix_operations.md)。

/// Ledoit-Wolf 协方差收缩 (OAS 闭式, Chen-Wiesel-Eldar-Hero 2010)
/// 比经典 LW 数值更稳, 无超参.
///
/// 公式:  Σ̂ = (1-δ)·S + δ·F
///        F  = (tr(S)/N)·I
///        δ  = min(1, ((1-2/N)·tr(S²) + tr(S)²)
///                  / ((T+1-2/N)·(tr(S²) - tr(S)²/N)))
///
/// 输入: returns - Eigen::MatrixXd, N 行 × T 列 (行=标的, 列=时间点)
LedoitWolfResult ledoitWolfShrinkage(const Eigen::MatrixXd& returns) {
    LedoitWolfResult result;
    const Eigen::Index N = returns.rows();
    const Eigen::Index T = returns.cols();
    if (N < 2 || T < 2) return result;

    // Per-row mean (across columns) and centered matrix
    Eigen::VectorXd mu = returns.rowwise().mean();
    Eigen::MatrixXd centered = returns.rowwise() - mu.transpose();

    // Sample covariance S = (T-1)^-1 · centered · centeredᵀ
    Eigen::MatrixXd S = (centered * centered.transpose()) / static_cast<double>(T - 1);

    // OAS shrinkage intensity
    const double tr_S = S.trace();
    const double tr_S2 = S.squaredNorm();
    const double num = (1.0 - 2.0 / N) * tr_S2 + tr_S * tr_S;
    const double den = (T + 1.0 - 2.0 / N) * (tr_S2 - tr_S * tr_S / N);
    double delta = (den > 0.0) ? std::min(1.0, num / den) : 1.0;
    if (delta < 0.0) delta = 0.0;

    // Target: constant-variance diagonal F = (tr(S)/N)·I
    Eigen::MatrixXd F = Eigen::MatrixXd::Identity(N, N) * (tr_S / static_cast<double>(N));

    result._covariance = (1.0 - delta) * S + delta * F;
    result._shrinkage = delta;
    result._n_observations = static_cast<int>(T);
    result._n_variables = static_cast<int>(N);
    return result;
}

/// Risk Parity 权重 (Qian 2005 Spin-Glass 迭代)
///
/// 求解 w_i · (Σw)_i = const (各标的对组合方差贡献相等)。
/// 协方差自动由 ledoitWolfShrinkage 提供 (OAS 收缩).
///
/// 不收敛时返回 last-iter 权重 + converged=false, 不抛异常.
RiskParityResult riskParityWeights(const Eigen::MatrixXd& returns,
                                    double tolerance,
                                    int max_iterations) {
    RiskParityResult result;
    const Eigen::Index N = returns.rows();
    const Eigen::Index T = returns.cols();
    if (N < 2 || T < 2) return result;
    if (tolerance <= 0.0) tolerance = 1e-6;
    if (max_iterations <= 0) max_iterations = 200;

    LedoitWolfResult lw = ledoitWolfShrinkage(returns);
    if (lw._covariance.size() == 0) return result;
    const Eigen::MatrixXd& Sigma = lw._covariance;

    // Spin-Glass initialization: equal weight
    Eigen::VectorXd w = Eigen::VectorXd::Constant(N, 1.0 / static_cast<double>(N));
    const double target_share = 1.0 / static_cast<double>(N);

    result._converged = false;
    result._iterations = 0;

    Eigen::VectorXd rc;
    for (int iter = 0; iter < max_iterations; ++iter) {
        rc = w.cwiseProduct(Sigma * w);
        const double rc_mean = rc.mean();
        if (rc_mean < 1e-15) {
            // 退化协方差 (e.g., 全部零), 返回等权
            w = Eigen::VectorXd::Constant(N, 1.0 / static_cast<double>(N));
            rc = w.cwiseProduct(Sigma * w);
            break;
        }
        Eigen::VectorXd deviation = (rc.array() / rc_mean - 1.0).abs();
        const double max_dev = deviation.maxCoeff();
        result._iterations = iter;
        if (max_dev < tolerance) {
            result._converged = true;
            break;
        }
        // Spin-Glass update: w_i ← w_i × (target_share · RC̄ / RC_i), 归一化
        Eigen::VectorXd ratio = (target_share * rc_mean) * rc.cwiseInverse();
        w = w.cwiseProduct(ratio);
        const double sum = w.sum();
        if (sum < 1e-15) {
            w = Eigen::VectorXd::Constant(N, 1.0 / static_cast<double>(N));
        } else {
            w /= sum;
        }
    }

    rc = w.cwiseProduct(Sigma * w);
    Eigen::VectorXd final_dev = (rc.array() / rc.mean() - 1.0).abs();
    result._max_rc_deviation = final_dev.maxCoeff();
    result._weights = std::move(w);
    result._risk_contributions = std::move(rc);
    return result;
}

// ──────────────────────────────────────────────────────────────────────
// Marchenko-Pastur 谱指标 (m+/m-) 滚动窗口分析
// ──────────────────────────────────────────────────────────────────────

Vector<int> correlationCluster(const Eigen::MatrixXd& corr_matrix, int target_k) {
    int n = corr_matrix.rows();
    if (target_k >= n) {
        // 不需要降维，每个标的自成一簇
        Vector<int> labels(n);
        for (int i = 0; i < n; ++i) labels[i] = i;
        return labels;
    }

    // 贪心层次聚类 (single linkage): 反复合并最相关的两个簇
    // 距离定义为 d = 1 - |ρ|，越小越相似
    Vector<int> labels(n);
    Vector<bool> active(n, true);  // 簇是否还活跃
    Vector<Vector<int>> members(n);  // 每个簇包含的原始标的
    for (int i = 0; i < n; ++i) {
        labels[i] = i;
        members[i] = {i};
    }

    int num_clusters = n;
    while (num_clusters > target_k) {
        // 找最相关的两个活跃簇 (平均链接: 簇间平均 |ρ|)
        double best_sim = -1e9;
        int merge_i = -1, merge_j = -1;

        for (int i = 0; i < n; ++i) {
            if (!active[i]) continue;
            for (int j = i + 1; j < n; ++j) {
                if (!active[j]) continue;
                // 计算两簇间的平均 |ρ|
                double sum_abs_corr = 0;
                int count = 0;
                for (int a : members[i]) {
                    for (int b : members[j]) {
                        sum_abs_corr += std::abs(corr_matrix(a, b));
                        ++count;
                    }
                }
                double avg_sim = (count > 0) ? sum_abs_corr / count : 0;
                if (avg_sim > best_sim) {
                    best_sim = avg_sim;
                    merge_i = i;
                    merge_j = j;
                }
            }
        }

        if (merge_i < 0 || merge_j < 0) break;  // 无法继续合并

        // 合并 merge_j 到 merge_i
        for (int idx : members[merge_j]) {
            labels[idx] = merge_i;
            members[merge_i].push_back(idx);
        }
        members[merge_j].clear();
        active[merge_j] = false;
        --num_clusters;
    }

    // 重新编号为 0..target_k-1
    int new_label = 0;
    Vector<int> remap(n, -1);
    for (int i = 0; i < n; ++i) {
        if (active[i]) {
            remap[i] = new_label++;
        }
    }
    for (int i = 0; i < n; ++i) {
        labels[i] = remap[labels[i]];
    }

    return labels;
}

SpectrumIndicatorResult computeSpectrumIndicators(
    const Eigen::MatrixXd& ret_matrix,
    const Vector<String>& dates,
    int window_size,
    int max_clusters)
{
    SpectrumIndicatorResult result;
    int n = ret_matrix.rows();
    int T = ret_matrix.cols();

    if (n < 2 || T < window_size || window_size < 3) return result;

    result.original_n = n;
    result.window_size = window_size;

    // 确定有效标的数 k (若 n > window_size 则需降维)
    int k = n;
    bool need_clustering = (n >= window_size);
    if (need_clustering) {
        k = std::min(max_clusters, window_size - 1);
        k = std::min(k, n);
    }

    // MP 边界 (基于 Q = window/k)
    double Q = static_cast<double>(window_size) / k;
    result.lambda_plus = std::pow(1.0 + 1.0 / std::sqrt(Q), 2);
    result.lambda_minus = std::pow(1.0 - 1.0 / std::sqrt(Q), 2);

    // 滚动窗口
    int num_windows = T - window_size + 1;
    result.dates.reserve(num_windows);
    result.m_plus.reserve(num_windows);
    result.m_minus.reserve(num_windows);
    result.n_effective.reserve(num_windows);

    for (int w_start = 0; w_start < num_windows; ++w_start) {
        int w_end = w_start + window_size;

        // 提取窗口内的收益率子矩阵
        Eigen::MatrixXd window_ret = ret_matrix.middleCols(w_start, window_size);

        // 计算相关矩阵
        Eigen::MatrixXd corr;
        int effective_n;

        if (need_clustering) {
            // 先用全窗口数据做聚类 (聚类结构相对稳定，不需要每窗口重算)
            // 但为简单起见，这里每窗口重算 (可优化)
            Eigen::MatrixXd full_corr;
            {
                Eigen::VectorXd means = window_ret.rowwise().mean();
                Eigen::MatrixXd centered = window_ret.colwise() - means;
                Eigen::MatrixXd cov = (centered * centered.transpose()) / (window_size - 1);
                Eigen::VectorXd std_devs = cov.diagonal().array().sqrt();
                for (int i = 0; i < n; ++i) {
                    if (std_devs(i) < 1e-10) std_devs(i) = 1.0;
                }
                full_corr.resize(n, n);
                for (int i = 0; i < n; ++i) {
                    for (int j = 0; j < n; ++j) {
                        full_corr(i, j) = cov(i, j) / (std_devs(i) * std_devs(j));
                    }
                }
                full_corr.diagonal().setOnes();
            }

            // 聚类降维
            Vector<int> labels = correlationCluster(full_corr, k);

            // 构建簇代表组合 (等权平均)
            Eigen::MatrixXd cluster_ret = Eigen::MatrixXd::Zero(k, window_size);
            Vector<int> cluster_counts(k, 0);
            for (int i = 0; i < n; ++i) {
                int c = labels[i];
                cluster_ret.row(c) += window_ret.row(i);
                cluster_counts[c]++;
            }
            for (int c = 0; c < k; ++c) {
                if (cluster_counts[c] > 0) {
                    cluster_ret.row(c) /= cluster_counts[c];
                }
            }

            // 计算簇间相关矩阵
            Eigen::VectorXd cluster_means = cluster_ret.rowwise().mean();
            Eigen::MatrixXd centered = cluster_ret.colwise() - cluster_means;
            Eigen::MatrixXd cov = (centered * centered.transpose()) / (window_size - 1);
            Eigen::VectorXd std_devs = cov.diagonal().array().sqrt();
            for (int i = 0; i < k; ++i) {
                if (std_devs(i) < 1e-10) std_devs(i) = 1.0;
            }
            corr.resize(k, k);
            for (int i = 0; i < k; ++i) {
                for (int j = 0; j < k; ++j) {
                    corr(i, j) = cov(i, j) / (std_devs(i) * std_devs(j));
                }
            }
            corr.diagonal().setOnes();
            effective_n = k;
        } else {
            // 无需降维
            Eigen::VectorXd means = window_ret.rowwise().mean();
            Eigen::MatrixXd centered = window_ret.colwise() - means;
            Eigen::MatrixXd cov = (centered * centered.transpose()) / (window_size - 1);
            Eigen::VectorXd std_devs = cov.diagonal().array().sqrt();
            for (int i = 0; i < n; ++i) {
                if (std_devs(i) < 1e-10) std_devs(i) = 1.0;
            }
            corr.resize(n, n);
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    corr(i, j) = cov(i, j) / (std_devs(i) * std_devs(j));
                }
            }
            corr.diagonal().setOnes();
            effective_n = n;
        }

        // 特征值分解
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(corr);
        Eigen::VectorXd eigenvalues = es.eigenvalues().real();

        // 降序排序
        std::vector<double> sorted_evals(eigenvalues.data(), eigenvalues.data() + eigenvalues.size());
        std::sort(sorted_evals.begin(), sorted_evals.end(), std::greater<double>());

        // 计算 m+ 和 m-
        int m_plus = 0, m_minus = 0;
        double total_var = 0, signal_var = 0;
        for (double ev : sorted_evals) {
            total_var += ev;
            if (ev > result.lambda_plus) {
                ++m_plus;
                signal_var += ev;
            }
            if (ev < result.lambda_minus) ++m_minus;
        }

        result.m_plus.push_back(m_plus);
        result.m_minus.push_back(m_minus);
        result.n_effective.push_back(effective_n);
        result.signal_var_ratio.push_back(total_var > 0 ? signal_var / total_var : 0);
        result.lambda_max.push_back(sorted_evals[0]);
        result.lambda_max_ratio.push_back(result.lambda_plus > 0 ? sorted_evals[0] / result.lambda_plus : 0);
        result.dates.push_back(dates[w_end - 1]);  // 窗口结束日期
    }

    return result;
}

}
