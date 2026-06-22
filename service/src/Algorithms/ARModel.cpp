#include "Algorithms/ARModel.h"
#include "Util/system.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace ar_model {

// ============================================================
// findSignificantLag — 找到 ACF 中最后一个显著 lag
// ============================================================

int findSignificantLag(const std::vector<double>& acf, int N, int max_p) {
    if (acf.size() < 2 || N < 30) return 0;
    double threshold = CONFIDENCE_Z / std::sqrt(static_cast<double>(N));  // 95% 置信带
    int result = 0;
    int limit = std::min(max_p, static_cast<int>(acf.size()) - 1);
    for (int k = 1; k <= limit; ++k) {
        if (std::abs(acf[k]) > threshold) {
            result = k;  // 记录最后一个显著的 lag
        }
    }
    return result;
}

// ============================================================
// solveYuleWalker — Levinson-Durbin 递推求解 AR(p) 系数
// ============================================================

ARFit solveYuleWalker(const std::vector<double>& acf, int p) {
    ARFit fit;
    fit.order_p = p;
    if (p <= 0 || acf.size() < static_cast<size_t>(p + 1)) return fit;

    std::vector<double> phi(p + 1, 0);
    double var = 1.0 - acf[1] * acf[1];  // lag-1 残差方差
    if (var < 1e-15) { fit.stable = false; return fit; }
    phi[1] = acf[1];

    for (int k = 2; k <= p; ++k) {
        double num = acf[k];
        for (int j = 1; j < k; ++j) num -= phi[j] * acf[k - j];
        double den = 1.0;
        for (int j = 1; j < k; ++j) den -= phi[j] * acf[j];
        if (std::abs(den) < 1e-15) { fit.stable = false; break; }

        double phi_kk = num / den;
        std::vector<double> phi_old = phi;
        phi[k] = phi_kk;
        for (int j = 1; j < k; ++j) phi[j] = phi_old[j] - phi_kk * phi_old[k - j];

        var *= (1.0 - phi_kk * phi_kk);
        if (var < 1e-15) { fit.stable = false; break; }
    }

    fit.coeffs.assign(phi.begin() + 1, phi.end());
    fit.residual_var = std::max(0.0, var);
    return fit;
}

// ============================================================
// predict — AR(p) 向前预测
// ============================================================

Forecast predict(const std::vector<double>& coeffs,
                 const std::vector<double>& history,
                 int steps, double var) {
    Forecast fc;
    int p = coeffs.size();
    if (p == 0 || history.empty() || steps <= 0) {
        fc.values.assign(steps, 0);
        double sigma = std::sqrt(std::max(0.0, var));
        fc.std_per_step.assign(steps, sigma);
        return fc;
    }

    // 用最近 p 个值初始化缓冲区
    std::vector<double> buf;
    if (history.size() >= static_cast<size_t>(p)) {
        buf.assign(history.end() - p, history.end());
    } else {
        buf = history;
        buf.insert(buf.begin(), p - buf.size(), 0);
    }

    double sigma = std::sqrt(std::max(0.0, var));

    // MA 系数累积: ψ_h = Σ φ_i * ψ_{h-i}, ψ_0 = 1
    std::vector<double> psi(steps, 0);
    psi[0] = 1.0;

    for (int h = 0; h < steps; ++h) {
        // 点预测: ŷ = Σ φ_i * y_{h-i}
        double yh = 0;
        size_t buf_end = buf.size();
        for (int i = 0; i < p; ++i) yh += coeffs[i] * buf[buf_end - 1 - i];
        fc.values.push_back(yh);
        buf.push_back(yh);  // 用预测值回填

        // 计算 ψ_h (MA 系数)
        if (h > 0) {
            double psi_h = 0;
            for (int i = 0; i < std::min(p, h); ++i) {
                psi_h += coeffs[i] * psi[h - 1 - i];
            }
            psi[h] = psi_h;
        }

        // h+1 步标准差: σ_h = σ * sqrt(Σ_{j=0}^{h} ψ_j²)
        double cum_psi_sq = 0;
        for (int j = 0; j <= h; ++j) cum_psi_sq += psi[j] * psi[j];
        fc.std_per_step.push_back(sigma * std::sqrt(cum_psi_sq));
    }

    return fc;
}

// ============================================================
// buildForecast — 构建完整预测结果
// ============================================================

ForecastResult buildForecast(const std::vector<double>& series,
                              const std::vector<double>& acf,
                              int max_p,
                              const String& source,
                              double last_known) {
    ForecastResult result;
    result.source_series = source;

    if (series.size() < 30 || acf.size() < 2) return result;

    int p = findSignificantLag(acf, series.size(), max_p);
    if (p == 0) return result;

    auto fit = solveYuleWalker(acf, p);
    if (!fit.stable || fit.coeffs.empty()) return result;

    // === 关键修复：将归一化残差方差转换到原始尺度 ===
    // solveYuleWalker 在归一化 ACF 上计算，返回的 residual_var 是相对于方差=1 的
    // 需要乘以实际序列方差才能得到原始尺度上的残差方差
    double sample_var = 0;
    if (!series.empty()) {
        double m = std::accumulate(series.begin(), series.end(), 0.0) / series.size();
        for (auto v : series) sample_var += (v - m) * (v - m);
        sample_var /= series.size();  // 使用 N（与 solveYuleWalker 一致）
    }
    double actual_residual_var = fit.residual_var * sample_var;

    result.order_p = p;
    result.ar_coeffs = fit.coeffs;
    result.residual_var = actual_residual_var;
    result.has_autocorrelation = true;

    // 取最后 p 个观测值
    std::vector<double> history(series.end() - p, series.end());
    auto fc = predict(fit.coeffs, history, p, actual_residual_var);

    result.forecast_values = fc.values;
    result.forecast_std = fc.std_per_step;

    // 置信区间（基于 last_known 推导）
    double base = last_known;
    for (size_t i = 0; i < fc.values.size(); ++i) {
        double s = fc.std_per_step[i];
        // 预测值 + 置信区间
        result.forecast_upper_1sigma.push_back(base + fc.values[i] + s);
        result.forecast_lower_1sigma.push_back(base + fc.values[i] - s);
        result.forecast_upper_2sigma.push_back(base + fc.values[i] + 2 * s);
        result.forecast_lower_2sigma.push_back(base + fc.values[i] - 2 * s);
    }

    // 诊断信息
    double r2 = (sample_var > 1e-15) ? (1.0 - fit.residual_var) : 0;
    result.note = fmt::format("AR({}) from {}, R²≈{:.2f}", p, source, r2);

    return result;
}

// ============================================================
// extrapolateCovariance — 多资产协方差外推
// ============================================================

std::vector<std::vector<double>> extrapolateCovariance(
    const std::vector<std::vector<double>>& base_cov,
    const std::vector<Forecast>& forecasts,
    int horizon)
{
    int N = base_cov.size();
    if (N == 0 || horizon <= 0 || forecasts.empty()) return base_cov;

    auto result = base_cov;

    // 取 horizon-1 步的 std（最后一档）
    std::vector<double> inflation(N);
    for (int i = 0; i < N; ++i) {
        int h = std::min(horizon - 1, static_cast<int>(forecasts[i].std_per_step.size()) - 1);
        inflation[i] = forecasts[i].std_per_step[h];
    }

    // Σ_forecast = Σ_base ⊙ (σ_i × σ_j)
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            result[i][j] = base_cov[i][j] * inflation[i] * inflation[j];
        }
    }

    return result;
}

}  // namespace ar_model
