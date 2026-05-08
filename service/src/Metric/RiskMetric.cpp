#include "Metric/RiskMetric.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <sstream>
#include <iomanip>
#include <iostream>

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

// ============================================================
// Bootstrap 辅助函数
// ============================================================

struct PathResult {
    double total_return;
    double max_drawdown;
    bool ruined_50;
    bool ruined_30;
};

static PathResult simulate_path(const std::vector<double>& sampled_returns) {
    double cumulative = 1.0;
    double peak = cumulative;
    double max_dd = 0.0;

    for (double ret : sampled_returns) {
        cumulative *= (1.0 + ret);
        if (cumulative > peak) {
            peak = cumulative;
        }
        double dd = (peak - cumulative) / peak;
        if (dd > max_dd) {
            max_dd = dd;
        }
    }

    return PathResult{
        cumulative - 1.0,
        max_dd,
        cumulative < 0.5,
        cumulative < 0.3
    };
}

static std::vector<double> build_stress_returns(const std::vector<double>& returns) {
    // 计算原始波动率
    double mean = 0.0;
    for (double r : returns) mean += r;
    mean /= returns.size();

    double var = 0.0;
    for (double r : returns) {
        double d = r - mean;
        var += d * d;
    }
    var /= (returns.size() - 1);
    double std_dev = std::sqrt(var);

    // 波动率 × 1.5，保持均值不变
    double stress_factor = 1.5;
    std::vector<double> stress;
    stress.reserve(returns.size());
    for (double r : returns) {
        stress.push_back(mean + (r - mean) * stress_factor);
    }
    return stress;
}

static BootstrapResult aggregate_results(
    const std::vector<PathResult>& paths,
    int n_days,
    int n_simulations,
    int method,
    int block_size,
    double acf
) {
    int n = static_cast<int>(paths.size());

    // 排序用于分位数
    std::vector<double> sorted_returns;
    std::vector<double> sorted_dd;
    sorted_returns.reserve(n);
    sorted_dd.reserve(n);
    int ruined_50 = 0;
    int ruined_30 = 0;

    for (const auto& p : paths) {
        sorted_returns.push_back(p.total_return);
        sorted_dd.push_back(p.max_drawdown);
        if (p.ruined_50) ruined_50++;
        if (p.ruined_30) ruined_30++;
    }

    std::sort(sorted_returns.begin(), sorted_returns.end());
    std::sort(sorted_dd.begin(), sorted_dd.end());

    BootstrapResult result{};
    result._ruin_prob_50 = static_cast<float>(ruined_50) / n_simulations;
    result._ruin_prob_30 = static_cast<float>(ruined_30) / n_simulations;
    result._return_p5  = static_cast<float>(sorted_returns[std::max(0, int(0.05 * n))]);
    result._return_p50 = static_cast<float>(sorted_returns[std::max(0, int(0.50 * n))]);
    result._return_p95 = static_cast<float>(sorted_returns[std::min(n - 1, int(0.95 * n))]);
    result._max_dd_p50 = static_cast<float>(sorted_dd[std::max(0, int(0.50 * n))]);
    result._max_dd_p95 = static_cast<float>(sorted_dd[std::min(n - 1, int(0.95 * n))]);
    result._median_annual_ret = static_cast<float>(
        std::pow(1.0 + sorted_returns[std::max(0, int(0.50 * n))], 252.0 / n_days) - 1.0
    );
    result._tail_1pct_avg_dd = 0.0f;
    {
        int tail_count = std::max(1, int(0.01 * n));
        double sum = 0.0;
        for (int i = 0; i < tail_count; ++i) sum += sorted_dd[i];
        result._tail_1pct_avg_dd = static_cast<float>(sum / tail_count);
    }

    result._method = method;
    result._block_size = block_size;
    result._autocorrelation = static_cast<float>(acf);
    result._n_simulations = n_simulations;

    return result;
}

// ============================================================
// Bootstrap 主函数
// ============================================================

BootstrapResult bootstrap_analysis(
    const std::vector<double>& daily_returns,
    double initial_capital,
    int n_simulations,
    unsigned seed
) {
    BootstrapResult result{};

    if (daily_returns.size() < 10 || n_simulations <= 0) {
        return result;
    }

    int n_days = static_cast<int>(daily_returns.size());

    // 1. 自相关检验
    double acf = compute_autocorrelation(daily_returns, 1);
    bool use_block = std::abs(acf) >= 0.1;
    int block_size = use_block ? std::max(5, static_cast<int>(252.0 / n_days * 10)) : 0;

    // 初始化随机数生成器
    std::mt19937 gen(seed == 0 ? std::random_device{}() : seed);
    std::uniform_int_distribution<size_t> dist_idx(0, n_days - 1);

    // 2. 普通 / Block Bootstrap
    std::vector<PathResult> paths;
    paths.reserve(n_simulations);

    if (!use_block) {
        // 普通 Bootstrap：独立抽样
        for (int sim = 0; sim < n_simulations; ++sim) {
            std::vector<double> sampled;
            sampled.reserve(n_days);
            for (int i = 0; i < n_days; ++i) {
                sampled.push_back(daily_returns[dist_idx(gen)]);
            }
            paths.push_back(simulate_path(sampled));
        }
    } else {
        // Block Bootstrap：抽连续块
        // 块大小自适应：至少 5 天，最多 n_days/3
        int blk = std::max(5, std::min(n_days / 3, block_size));

        std::uniform_int_distribution<size_t> dist_start(0, n_days - blk);

        for (int sim = 0; sim < n_simulations; ++sim) {
            std::vector<double> sampled;
            sampled.reserve(n_days);

            int remaining = n_days;
            while (remaining > 0) {
                size_t start = dist_start(gen);
                int take = std::min(blk, remaining);
                for (int i = 0; i < take; ++i) {
                    sampled.push_back(daily_returns[start + i]);
                }
                remaining -= take;
            }
            paths.push_back(simulate_path(sampled));
        }
    }

    // 汇总主结果
    result = aggregate_results(paths, n_days, n_simulations,
                               use_block ? 1 : 0, block_size, acf);

    // 3. 压力测试：波动率 × 1.5
    std::vector<double> stress_returns = build_stress_returns(daily_returns);
    std::vector<PathResult> stress_paths;
    stress_paths.reserve(n_simulations);

    if (!use_block) {
        for (int sim = 0; sim < n_simulations; ++sim) {
            std::vector<double> sampled;
            sampled.reserve(n_days);
            for (int i = 0; i < n_days; ++i) {
                sampled.push_back(stress_returns[dist_idx(gen)]);
            }
            stress_paths.push_back(simulate_path(sampled));
        }
    } else {
        int blk = std::max(5, std::min(n_days / 3, block_size));
        std::uniform_int_distribution<size_t> dist_start(0, n_days - blk);

        for (int sim = 0; sim < n_simulations; ++sim) {
            std::vector<double> sampled;
            sampled.reserve(n_days);
            int remaining = n_days;
            while (remaining > 0) {
                size_t start = dist_start(gen);
                int take = std::min(blk, remaining);
                for (int i = 0; i < take; ++i) {
                    sampled.push_back(stress_returns[start + i]);
                }
                remaining -= take;
            }
            stress_paths.push_back(simulate_path(sampled));
        }
    }

    // 汇总压力测试结果
    int n = n_simulations;
    std::vector<double> stress_sorted_returns;
    std::vector<double> stress_sorted_dd;
    stress_sorted_returns.reserve(n);
    stress_sorted_dd.reserve(n);
    int stress_ruined_50 = 0;
    int stress_ruined_30 = 0;

    for (const auto& p : stress_paths) {
        stress_sorted_returns.push_back(p.total_return);
        stress_sorted_dd.push_back(p.max_drawdown);
        if (p.ruined_50) stress_ruined_50++;
        if (p.ruined_30) stress_ruined_30++;
    }
    std::sort(stress_sorted_returns.begin(), stress_sorted_returns.end());
    std::sort(stress_sorted_dd.begin(), stress_sorted_dd.end());

    result._stress_ruin_prob_50 = static_cast<float>(stress_ruined_50) / n;
    result._stress_ruin_prob_30 = static_cast<float>(stress_ruined_30) / n;
    result._stress_return_p5    = static_cast<float>(stress_sorted_returns[std::max(0, int(0.05 * n))]);
    result._stress_return_p50   = static_cast<float>(stress_sorted_returns[std::max(0, int(0.50 * n))]);
    result._stress_max_dd_p50   = static_cast<float>(stress_sorted_dd[std::max(0, int(0.50 * n))]);

    return result;
}

// ============================================================
// 格式化输出
// ============================================================

std::string bootstrap_result_to_string(const BootstrapResult& r) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);

    ss << "=== Bootstrap Monte Carlo Analysis ===\n";
    ss << "  Method:             " << (r._method == 0 ? "Standard Bootstrap" : "Block Bootstrap") << "\n";
    ss << "  Block Size:         " << r._block_size << "\n";
    ss << "  Autocorrelation:    " << r._autocorrelation << "\n";
    ss << "  Simulations:        " << r._n_simulations << "\n";
    ss << "\n";
    ss << "--- Normal Scenario ---\n";
    ss << "  Ruin Prob (<50%):   " << (r._ruin_prob_50 * 100.0f) << "%\n";
    ss << "  Ruin Prob (<30%):   " << (r._ruin_prob_30 * 100.0f) << "%\n";
    ss << "  Return P5:          " << (r._return_p5 * 100.0f) << "%\n";
    ss << "  Return P50:         " << (r._return_p50 * 100.0f) << "%\n";
    ss << "  Return P95:         " << (r._return_p95 * 100.0f) << "%\n";
    ss << "  MaxDD P50:          " << (r._max_dd_p50 * 100.0f) << "%\n";
    ss << "  MaxDD P95:          " << (r._max_dd_p95 * 100.0f) << "%\n";
    ss << "  Median Annual Ret:  " << (r._median_annual_ret * 100.0f) << "%\n";
    ss << "  Tail 1% Avg DD:     " << (r._tail_1pct_avg_dd * 100.0f) << "%\n";
    ss << "\n";
    ss << "--- Stress Test (Vol × 1.5) ---\n";
    ss << "  Ruin Prob (<50%):   " << (r._stress_ruin_prob_50 * 100.0f) << "%\n";
    ss << "  Ruin Prob (<30%):   " << (r._stress_ruin_prob_30 * 100.0f) << "%\n";
    ss << "  Return P5:          " << (r._stress_return_p5 * 100.0f) << "%\n";
    ss << "  Return P50:         " << (r._stress_return_p50 * 100.0f) << "%\n";
    ss << "  MaxDD P50:          " << (r._stress_max_dd_p50 * 100.0f) << "%\n";

    return ss.str();
}
