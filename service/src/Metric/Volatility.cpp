#include "Metric/Volatility.h"
#include "std_header.h"
#include <cmath>
#include <numeric>

double compute_annualized_volatility(const std::vector<double>& daily_returns) {
    if (daily_returns.size() < 2) {
        return 0.0f;
    }

    // 计算均值
    double sum = 0.0;
    for (double ret : daily_returns) {
        sum += ret;
    }
    double mean = sum / daily_returns.size();

    // 计算样本标准差（Bessel 修正）
    double sum_sq = 0.0;
    for (double ret : daily_returns) {
        double diff = ret - mean;
        sum_sq += diff * diff;
    }
    double std_daily = std::sqrt(sum_sq / (daily_returns.size() - 1));

    // 年化
    return static_cast<float>(std_daily * std::sqrt(static_cast<double>(YEAR_DAY)));
}

std::vector<double> rolling_volatility(const std::vector<double>& returns, int window) {
    std::vector<double> result;
    if (returns.size() < static_cast<size_t>(window)) return result;

    for (size_t i = window - 1; i < returns.size(); ++i) {
        double sum = 0;
        for (int j = 0; j < window; ++j) sum += returns[i - j];
        double mean = sum / window;
        double var = 0;
        for (int j = 0; j < window; ++j) {
            double d = returns[i - j] - mean;
            var += d * d;
        }
        var /= (window - 1);
        result.push_back(std::sqrt(var) * std::sqrt(252.0));
    }
    return result;
}
