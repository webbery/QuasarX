#include "Metric/RiskMetric.h"
#include <Eigen/Dense>
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
// 自相关函数（Eigen 实现）
// ============================================================

double compute_autocorrelation(const std::vector<double>& returns, int lag) {
    int n = static_cast<int>(returns.size());
    if (n <= lag || n < 2) {
        return 0.0;
    }

    // 使用 Eigen 向量化计算
    Eigen::Map<const Eigen::VectorXd> x(returns.data(), n);
    double mean = x.mean();
    Eigen::VectorXd centered = x.array() - mean;

    // 方差：sum((x - mean)²)
    double variance = centered.squaredNorm();

    // 协方差：sum((x_t - mean) * (x_{t+lag} - mean))
    double covariance = centered.head(n - lag).dot(centered.tail(n - lag));

    if (variance < 1e-10) {
        return 0.0;
    }

    return covariance / variance;
}

// ============================================================
// 相关系数计算（Eigen 实现）
// ============================================================

/**
 * @brief 计算两个收益率向量的相关系数
 * @param x 收益率序列 1
 * @param y 收益率序列 2
 * @return 相关系数 [-1, 1]
 */
double compute_correlation(const std::vector<double>& x, const std::vector<double>& y) {
    size_t n = x.size();
    if (n != y.size() || n < 2) {
        return 0.0;
    }

    Eigen::Map<const Eigen::VectorXd> vx(x.data(), n);
    Eigen::Map<const Eigen::VectorXd> vy(y.data(), n);

    double mx = vx.mean();
    double my = vy.mean();

    Eigen::VectorXd cx = vx.array() - mx;
    Eigen::VectorXd cy = vy.array() - my;

    double denom = std::sqrt(cx.squaredNorm() * cy.squaredNorm());
    if (denom < 1e-10) {
        return 0.0;
    }

    return cx.dot(cy) / denom;
}

/**
 * @brief 计算收益率矩阵的相关系数矩阵
 * @param data 输入矩阵 (n_assets × n_samples)，每行是一个资产的收益率序列
 * @return 相关系数矩阵 (n_assets × n_assets)
 */
Eigen::MatrixXd compute_correlation_matrix(const Eigen::MatrixXd& data) {
    size_t n_assets = data.rows();
    size_t n_samples = data.cols();

    if (n_samples < 2) {
        return Eigen::MatrixXd::Identity(n_assets, n_assets);
    }

    // 1. 去均值（每行减均值）
    Eigen::MatrixXd centered = data;
    for (size_t i = 0; i < n_assets; ++i) {
        centered.row(i).array() -= data.row(i).mean();
    }

    // 2. 计算协方差矩阵：C = (X * X^T) / (n-1)
    Eigen::MatrixXd cov = (centered * centered.transpose()) / (n_samples - 1);

    // 3. 转换为相关系数矩阵：ρ_ij = cov_ij / (σ_i * σ_j)
    Eigen::VectorXd std_devs(n_assets);
    for (size_t i = 0; i < n_assets; ++i) {
        std_devs(i) = std::sqrt(cov(i, i));
        if (std_devs(i) < 1e-10) std_devs(i) = 1.0;  // 防止除以零
    }

    Eigen::MatrixXd corr = cov;
    for (size_t i = 0; i < n_assets; ++i) {
        for (size_t j = 0; j < n_assets; ++j) {
            corr(i, j) = cov(i, j) / (std_devs(i) * std_devs(j));
        }
    }

    // 确保对角线为 1
    corr.diagonal().setOnes();

    return corr;
}
