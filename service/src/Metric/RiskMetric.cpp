#include "Metric/RiskMetric.h"
#include <algorithm>
#include <cmath>
#include <numeric>

// ============================================================
// VaR / CVaR
// ============================================================

float compute_var(const std::vector<double>& returns, double confidence_level) {
    if (returns.empty()) {
        return 0.0f;
    }

    std::vector<double> sorted_returns = returns;
    std::sort(sorted_returns.begin(), sorted_returns.end());

    double quantile = 1.0 - confidence_level;
    size_t index = static_cast<size_t>(quantile * sorted_returns.size());

    if (index >= sorted_returns.size()) {
        index = sorted_returns.size() - 1;
    }

    return static_cast<float>(-sorted_returns[index]);
}

float compute_cvar(const std::vector<double>& returns, double confidence_level) {
    if (returns.empty()) {
        return 0.0f;
    }

    std::vector<double> sorted_returns = returns;
    std::sort(sorted_returns.begin(), sorted_returns.end());

    double quantile = 1.0 - confidence_level;
    size_t var_index = static_cast<size_t>(quantile * sorted_returns.size());

    if (var_index >= sorted_returns.size()) {
        var_index = sorted_returns.size() - 1;
    }

    double var_threshold = sorted_returns[var_index];

    double sum = 0.0;
    int count = 0;
    for (size_t i = 0; i <= var_index && i < sorted_returns.size(); ++i) {
        sum += sorted_returns[i];
        count++;
    }

    if (count == 0) {
        return 0.0f;
    }

    return static_cast<float>(-sum / count);
}

// ============================================================
// EWMA VaR
// ============================================================

float compute_ewma_var(const std::vector<double>& returns, double confidence_level, double decay) {
    if (returns.empty()) {
        return 0.0f;
    }

    // EWMA 方差计算：σ²_t = λ * σ²_{t-1} + (1-λ) * r²_{t-1}
    double ewma_var = returns[0] * returns[0];  // 初始方差
    for (size_t i = 1; i < returns.size(); ++i) {
        ewma_var = decay * ewma_var + (1.0 - decay) * returns[i] * returns[i];
    }

    double ewma_std = std::sqrt(std::max(0.0, ewma_var));

    // 正态分布假设下的 VaR：VaR = -σ * z_quantile
    // z_0.05 ≈ 1.645, z_0.01 ≈ 2.326
    double alpha = 1.0 - confidence_level;
    // 简化的逆正态分位数近似（Abramowitz & Stegun）
    double z = 0.0;
    if (alpha < 0.5) {
        double t = std::sqrt(-2.0 * std::log(alpha));
        z = t - (2.515517 + 0.802853 * t + 0.010328 * t * t) /
                  (1.0 + 1.432788 * t + 0.189269 * t * t + 0.001308 * t * t * t);
    } else {
        // 不应该走到这里（confidence_level 通常 >= 0.9）
        z = 1.645;
    }

    return static_cast<float>(ewma_std * z);
}

// ============================================================
// 自适应 VaR
// ============================================================

float compute_adaptive_var(const std::vector<double>& returns,
                           const CUSUMDetector& detector,
                           const AdaptiveVaRConfig& config) {
    if (returns.empty()) {
        return 0.0f;
    }

    // 判断是否处于"压力状态"：最后 normal_window 天内是否有变点
    bool in_stress = false;
    size_t recent_window = std::min(config.normal_window, returns.size());
    size_t start_index = returns.size() - recent_window;

    // 检查 detector 是否在最近窗口内检测到变点
    if (detector.get_total_change_points() > 0) {
        size_t last_change = detector.get_last_change_index();
        if (last_change >= start_index) {
            in_stress = true;
        }
    }

    // 选择窗口和置信度
    size_t window = in_stress ?
        std::min(config.stressed_window, returns.size()) :
        std::min(config.normal_window, returns.size());

    double conf = in_stress ? config.stressed_confidence : config.confidence;

    // 截取窗口数据
    size_t start = returns.size() - window;
    std::vector<double> window_returns(returns.begin() + start, returns.end());

    if (in_stress && config.enable_ewma_fallback) {
        // 压力状态：EWMA VaR（对分布变化更敏感）
        return compute_ewma_var(window_returns, conf, config.ewma_decay);
    } else {
        // 正常状态：历史模拟法
        return compute_var(window_returns, conf);
    }
}

// ============================================================
// 自相关函数
// ============================================================

double compute_autocorrelation(const std::vector<double>& returns, int lag) {
    int n = static_cast<int>(returns.size());
    if (n <= lag || n < 2) {
        return 0.0;
    }

    // 计算均值
    double mean = 0.0;
    for (double r : returns) {
        mean += r;
    }
    mean /= n;

    // 计算方差和协方差
    double variance = 0.0;
    double covariance = 0.0;
    for (int i = 0; i < n; ++i) {
        double diff_i = returns[i] - mean;
        variance += diff_i * diff_i;

        if (i + lag < n) {
            double diff_j = returns[i + lag] - mean;
            covariance += diff_i * diff_j;
        }
    }

    if (variance == 0.0) {
        return 0.0;
    }

    return covariance / variance;
}
