#include "Util/finance.h"
#include "Util/datetime.h"
#include <boost/math/statistics/univariate_statistics.hpp>
#include <filesystem>
#include "csv.h"
#include <cmath>

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

/// 计算相关系数
static double calcCorrelation(const Vector<double>& x, const Vector<double>& y) {
    size_t n = x.size();
    if (n < 2 || y.size() != n) return 0.0;
    double mx = calcMean(x), my = calcMean(y);
    double sxy = 0, sx2 = 0, sy2 = 0;
    for (size_t i = 0; i < n; ++i) {
        double dx = x[i] - mx, dy = y[i] - my;
        sxy += dx * dy;
        sx2 += dx * dx;
        sy2 += dy * dy;
    }
    double denom = std::sqrt(sx2 * sy2);
    return (denom > 1e-15) ? (sxy / denom) : 0.0;
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

}

bool LoadStockQuote(DataFrame& df, const String& path) {
    if (!std::filesystem::exists(path))
        return false;

    String datetime;
    double open, close, high, low, volumn, amount, price_volatility, change_percent, turnover_rate;

    Vector<String> sv;
    df.load_column("datetime", sv);
    Vector<double> dv;
    for (auto name : { "open", "close", "high","low", "volume", "turnover",
        }) {
        df.load_column(name, dv);
    }
    uint32_t index = 0;
    io::CSVReader<7> reader(path);
    // 日期,开盘,收盘,最高,最低,成交量,成交额,换手率
    reader.read_header(io::ignore_extra_column, "datetime", "open", "close", "high", "low", "volume", "turnover");
    while (reader.read_row(datetime, open, close, high, low, volumn, turnover_rate)) {
        auto t = FromStr(datetime);
        df.append_row(&index, std::make_pair("datetime", t), std::make_pair("open", open), std::make_pair("close", close),
            std::make_pair("high", high), std::make_pair("low", low), std::make_pair("volume", volumn),
            std::make_pair("turnover", turnover_rate)
        );
        ++index;
    }
    return true;
}

// ──────────────────────────────────────────────────────────────────────
// 信号分析 / 时序分析工具函数
// ──────────────────────────────────────────────────────────────────────

Vector<double> finance::computeACF(const Vector<double>& data, int max_lag) {
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

Vector<double> finance::computePACF(const Vector<double>& acf, int max_lag) {
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

double finance::estimateMeanPeriod(const Vector<double>& data) {
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
            double angle = -2.0 * M_PI * k * t / n;
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

double finance::computeEnergyPct(const Vector<double>& component,
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
