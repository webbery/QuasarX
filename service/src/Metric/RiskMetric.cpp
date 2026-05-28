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
