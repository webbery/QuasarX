#include "Metric/MonteCarloSimulator.h"
#include <numeric>

// ============================================================
// 初始化
// ============================================================

bool MonteCarloSimulator::Init(const McSimConfig& config) {
    _config = config;

    // 初始化随机数生成器
    if (_config.seed != 0) {
        _rng.seed(_config.seed);
    } else {
        _rng.seed(std::random_device{}());
    }

    return true;
}

void MonteCarloSimulator::FeedReturns(const std::vector<double>& net_returns) {
    _returns = net_returns;
    _nBars = static_cast<int>(net_returns.size());

    if (_nBars < 10) {
        return;
    }

    // 预分配采样缓冲区（避免 Run() 循环内反复分配）
    _sampledBuf.resize(_nBars);
    _stressReturnsBuf.resize(_nBars);

    // 初始化复用的 distribution
    _distIdx = std::uniform_int_distribution<size_t>(0, _nBars - 1);

    // 预计算自相关
    _acf = computeAutocorrelation(_returns, 1);

    // 预计算原始标准差（用于 vol_ratio）
    _originalStd = computeOriginalStd();

    // 决定 Bootstrap 方法
    switch (_config.bootstrap_method) {
        case BootstrapMethod::Auto:
            _useBlockBootstrap = std::abs(_acf) >= 0.1;
            break;
        case BootstrapMethod::Standard:
            _useBlockBootstrap = false;
            break;
        case BootstrapMethod::Block:
            _useBlockBootstrap = true;
            break;
    }

    if (_useBlockBootstrap) {
        _blockSize = computeBlockSize(_nBars);
        _distStart = std::uniform_int_distribution<size_t>(0, _nBars - _blockSize);
    }
}

// ============================================================
// 主入口
// ============================================================

McResult MonteCarloSimulator::Run(int n_simulations) {
    McResult result{};

    if (_nBars < 10) {
        return result;
    }

    int nSim = (n_simulations > 0) ? n_simulations : _config.n_simulations;

    // 1. 基础模拟
    std::vector<PathResult> paths;
    paths.reserve(nSim);

    for (int sim = 0; sim < nSim; ++sim) {
        if (_useBlockBootstrap) {
            sampleBlockBootstrapInto(_sampledBuf, _blockSize);
        } else {
            sampleStandardBootstrapInto(_sampledBuf);
        }
        paths.push_back(simulatePath(_sampledBuf));
    }

    result = aggregateResults(paths, _useBlockBootstrap ? 1 : 0, _blockSize, static_cast<float>(_acf));

    // 2. 压力测试
    if (_config.enable_stress_test) {
        // 2a. 基础压力：波动率 × N
        {
            buildStressReturnsInto(_stressReturnsBuf, _config.stress_vol_factor);
            std::vector<PathResult> stress_paths;
            stress_paths.reserve(nSim);

            for (int sim = 0; sim < nSim; ++sim) {
                if (_useBlockBootstrap) {
                    sampleBlockBootstrapFromInto(_stressReturnsBuf, _sampledBuf, _blockSize);
                } else {
                    sampleStandardBootstrapFromInto(_stressReturnsBuf, _sampledBuf);
                }
                stress_paths.push_back(simulatePath(_sampledBuf));
            }

            int stress_worst_count = _config.save_stress_worst_paths ? _config.save_stress_paths_count : 0;
            aggregateStressResults(stress_paths,
                result.stress_ruin_prob_high, result.stress_ruin_prob_low,
                result.stress_return_p5, result.stress_return_p50,
                result.stress_max_dd_p50,
                result.stress_worst_paths,
                stress_worst_count);
        }

        // 2b. 流动性压力：尾部收益率折扣
        if (_config.stress_liquidity) {
            // 先拷贝 _returns 到缓冲区
            std::vector<double> liq_returns = _returns;
            std::vector<double> sorted_for_tail = liq_returns;
            std::sort(sorted_for_tail.begin(), sorted_for_tail.end());

            int tail_count = std::max(1, static_cast<int>(_config.liquidity_tail_pct * _nBars));

            // 对最差 tail_count 个收益率应用折扣（简化实现：直接排序后修改）
            std::vector<size_t> indices(_nBars);
            std::iota(indices.begin(), indices.end(), 0);
            std::sort(indices.begin(), indices.end(),
                      [&](size_t a, size_t b) { return _returns[a] < _returns[b]; });

            for (int i = 0; i < tail_count; ++i) {
                liq_returns[indices[i]] *= _config.liquidity_factor;
            }

            std::vector<PathResult> liq_paths;
            liq_paths.reserve(nSim);

            for (int sim = 0; sim < nSim; ++sim) {
                if (_useBlockBootstrap) {
                    sampleBlockBootstrapFromInto(liq_returns, _sampledBuf, _blockSize);
                } else {
                    sampleStandardBootstrapFromInto(liq_returns, _sampledBuf);
                }
                liq_paths.push_back(simulatePath(_sampledBuf));
            }

            int liq_worst_count = _config.save_stress_worst_paths ? _config.save_stress_paths_count : 0;
            aggregateStressResults(liq_paths,
                result.liq_stress_ruin_prob_high, result.liq_stress_return_p5,
                result.liq_stress_return_p5 /* unused */,
                result.liq_stress_return_p5 /* unused */,
                result.liq_stress_max_dd_p50,
                result.liq_stress_worst_paths,
                liq_worst_count);
        }

        // 2c. 波动率聚集压力：更大 Block Size
        if (_config.stress_vol_clustering && _useBlockBootstrap) {
            int stress_blk = std::max(_blockSize,
                static_cast<int>(_blockSize * _config.vol_cluster_block_multiplier));
            stress_blk = std::min(stress_blk, _nBars / 2);

            std::vector<PathResult> vc_paths;
            vc_paths.reserve(nSim);
            std::uniform_int_distribution<size_t> dist_start(0, _nBars - stress_blk);

            for (int sim = 0; sim < nSim; ++sim) {
                sampleBlockBootstrapFromInto(_returns, _sampledBuf, stress_blk);
                vc_paths.push_back(simulatePath(_sampledBuf));
            }

            int vc_worst_count = _config.save_stress_worst_paths ? _config.save_stress_paths_count : 0;
            aggregateStressResults(vc_paths,
                result.vol_cluster_stress_ruin_prob_high, result.vol_cluster_stress_return_p5,
                result.vol_cluster_stress_return_p5 /* unused */,
                result.vol_cluster_stress_return_p5 /* unused */,
                result.vol_cluster_stress_max_dd_p50,
                result.vol_cluster_worst_paths,
                vc_worst_count);
        }
    }

    return result;
}

// ============================================================
// 单条路径模拟（完整记录）
// ============================================================

MonteCarloSimulator::PathResult MonteCarloSimulator::simulatePath(const std::vector<double>& sampled_returns) {
    double cumulative = _config.initial_capital;
    double peak = cumulative;
    double max_dd = 0.0;

    // 记录完整资金曲线
    std::vector<double> equity_curve;
    equity_curve.reserve(sampled_returns.size() + 1);
    equity_curve.push_back(cumulative);

    // 连胜/连亏追踪
    int current_win_streak = 0;
    int current_loss_streak = 0;
    int longest_win = 0;
    int longest_loss = 0;
    int win_bars = 0;

    // 峰值/谷值位置
    size_t peak_bar = 0;
    size_t trough_bar = 0;
    size_t max_dd_bar = 0;
    double trough_val = cumulative;

    for (size_t i = 0; i < sampled_returns.size(); ++i) {
        double ret = sampled_returns[i];
        cumulative *= (1.0 + ret);

        // 资金曲线
        equity_curve.push_back(cumulative);

        // 峰值更新
        if (cumulative > peak) {
            peak = cumulative;
            peak_bar = i + 1;
        }

        // 回撤计算
        double dd = (peak - cumulative) / peak;
        if (dd > max_dd) {
            max_dd = dd;
            max_dd_bar = i + 1;
        }

        // 谷值更新
        if (cumulative < trough_val) {
            trough_val = cumulative;
            trough_bar = i + 1;
        }

        // 连胜/连亏
        if (ret > 0) {
            current_win_streak++;
            current_loss_streak = 0;
            win_bars++;
            if (current_win_streak > longest_win) longest_win = current_win_streak;
        } else if (ret < 0) {
            current_loss_streak++;
            current_win_streak = 0;
            if (current_loss_streak > longest_loss) longest_loss = current_loss_streak;
        }
    }

    double win_rate = (sampled_returns.empty()) ? 0.0 : static_cast<double>(win_bars) / sampled_returns.size();

    double high_threshold = _config.initial_capital * _ruinThresholdHigh;
    double low_threshold = _config.initial_capital * _ruinThresholdLow;

    return PathResult{
        cumulative - _config.initial_capital,
        max_dd,
        cumulative < high_threshold,
        cumulative < low_threshold,
        win_rate,
        longest_win,
        longest_loss,
        max_dd_bar,
        peak_bar,
        trough_bar,
        std::move(equity_curve)
    };
}

// ============================================================
// PathResult → PathDetail
// ============================================================

PathDetail MonteCarloSimulator::buildDetail(const PathResult& p) const {
    PathDetail d{};
    d.total_return = p.total_return;
    d.max_drawdown = p.max_drawdown;
    d.win_rate = p.win_rate;
    d.longest_win_streak = p.longest_win_streak;
    d.longest_loss_streak = p.longest_loss_streak;
    d.max_dd_bar_index = p.max_dd_bar_index;
    d.peak_bar_index = p.peak_bar_index;
    d.trough_bar_index = p.trough_bar_index;
    d.equity_curve = p.equity_curve;

    // vol_ratio = 该路径收益率标准差 / 原始序列标准差
    if (_originalStd > 0 && p.equity_curve.size() > 1) {
        // 从 equity_curve 反推收益率序列
        double mean = 0.0;
        std::vector<double> path_returns;
        path_returns.reserve(p.equity_curve.size() - 1);
        for (size_t i = 1; i < p.equity_curve.size(); ++i) {
            double r = (p.equity_curve[i] - p.equity_curve[i - 1]) / p.equity_curve[i - 1];
            path_returns.push_back(r);
            mean += r;
        }
        mean /= path_returns.size();

        double var = 0.0;
        for (double r : path_returns) {
            double d = r - mean;
            var += d * d;
        }
        double path_std = std::sqrt(var / (path_returns.size() - 1));
        d.vol_ratio = path_std / _originalStd;
    } else {
        d.vol_ratio = 1.0;
    }

    return d;
}

// ============================================================
// 抽样方法（预分配版本：写入外部缓冲区，避免分配）
// ============================================================

void MonteCarloSimulator::sampleStandardBootstrapInto(std::vector<double>& out) {
    for (int i = 0; i < _nBars; ++i) {
        out[i] = _returns[_distIdx(_rng)];
    }
}

void MonteCarloSimulator::sampleStandardBootstrapFromInto(
    const std::vector<double>& source, std::vector<double>& out) {
    for (int i = 0; i < _nBars; ++i) {
        out[i] = source[_distIdx(_rng)];
    }
}

void MonteCarloSimulator::sampleBlockBootstrapInto(std::vector<double>& out, int block_size) {
    int remaining = _nBars;
    int writePos = 0;
    while (remaining > 0) {
        size_t start = _distStart(_rng);
        int take = std::min(block_size, remaining);
        for (int i = 0; i < take; ++i) {
            out[writePos++] = _returns[start + i];
        }
        remaining -= take;
    }
}

void MonteCarloSimulator::sampleBlockBootstrapFromInto(
    const std::vector<double>& source, std::vector<double>& out, int block_size) {
    int remaining = _nBars;
    int writePos = 0;
    std::uniform_int_distribution<size_t> dist_start(0, source.size() - block_size);
    while (remaining > 0) {
        size_t start = dist_start(_rng);
        int take = std::min(block_size, remaining);
        for (int i = 0; i < take; ++i) {
            out[writePos++] = source[start + i];
        }
        remaining -= take;
    }
}

// ============================================================
// 旧版抽样方法（保留兼容，但不再使用）
// ============================================================

std::vector<double> MonteCarloSimulator::sampleStandardBootstrap() {
    std::vector<double> sampled;
    sampled.reserve(_nBars);
    std::uniform_int_distribution<size_t> dist_idx(0, _nBars - 1);

    for (int i = 0; i < _nBars; ++i) {
        sampled.push_back(_returns[dist_idx(_rng)]);
    }
    return sampled;
}

std::vector<double> MonteCarloSimulator::sampleBlockBootstrap(int block_size) {
    std::vector<double> sampled;
    sampled.reserve(_nBars);
    std::uniform_int_distribution<size_t> dist_start(0, _nBars - block_size);

    int remaining = _nBars;
    while (remaining > 0) {
        size_t start = dist_start(_rng);
        int take = std::min(block_size, remaining);
        for (int i = 0; i < take; ++i) {
            sampled.push_back(_returns[start + i]);
        }
        remaining -= take;
    }
    return sampled;
}

// ============================================================
// 压力测试收益率池构建（预分配版本）
// ============================================================

void MonteCarloSimulator::buildStressReturnsInto(std::vector<double>& out, double vol_factor) const {
    double mean = 0.0;
    for (double r : _returns) mean += r;
    mean /= _nBars;

    for (int i = 0; i < _nBars; ++i) {
        out[i] = mean + (_returns[i] - mean) * vol_factor;
    }
}

std::vector<double> MonteCarloSimulator::buildStressReturns(double vol_factor) const {
    double mean = 0.0;
    for (double r : _returns) mean += r;
    mean /= _nBars;

    std::vector<double> stress;
    stress.reserve(_nBars);
    for (double r : _returns) {
        stress.push_back(mean + (r - mean) * vol_factor);
    }
    return stress;
}

// ============================================================
// 结果聚合（含双向极端路径）
// ============================================================

McResult MonteCarloSimulator::aggregateResults(
    std::vector<PathResult>& paths,
    int method, int block_size, float acf
) {
    McResult result{};
    int n = static_cast<int>(paths.size());

    // 构建 PathDetail 列表
    std::vector<PathDetail> all_details;
    all_details.reserve(n);
    for (const auto& p : paths) {
        all_details.push_back(buildDetail(p));
    }

    // 按总收益率排序
    std::sort(all_details.begin(), all_details.end(),
              [](const PathDetail& a, const PathDetail& b) { return a.total_return < b.total_return; });

    // 保存最差路径
    if (_config.save_worst_paths_count > 0 && n > 0) {
        int count = std::min(_config.save_worst_paths_count, n);
        result.worst_paths.assign(all_details.begin(), all_details.begin() + count);
    }

    // 保存最好路径
    if (_config.save_best_paths_count > 0 && n > 0) {
        int count = std::min(_config.save_best_paths_count, n);
        result.best_paths.assign(all_details.end() - count, all_details.end());
    }

    // 保存分位数路径
    if (_config.save_percentile_paths && n > 0) {
        result.p10_path    = all_details[std::max(0, static_cast<int>(0.10 * n))];
        result.median_path = all_details[std::max(0, static_cast<int>(0.50 * n))];
        result.p90_path    = all_details[std::min(n - 1, static_cast<int>(0.90 * n))];
    }

    // === 基础统计指标 ===
    int ruined_high = 0;
    int ruined_low = 0;
    for (const auto& d : all_details) {
        double high_threshold = _config.initial_capital * _ruinThresholdHigh;
        double low_threshold = _config.initial_capital * _ruinThresholdLow;
        if ((_config.initial_capital + d.total_return) < high_threshold) ruined_high++;
        if ((_config.initial_capital + d.total_return) < low_threshold) ruined_low++;
    }

    result.ruin_prob_high = static_cast<float>(ruined_high) / n;
    result.ruin_prob_low = static_cast<float>(ruined_low) / n;
    result.return_p5 = static_cast<float>(all_details[std::max(0, static_cast<int>(0.05 * n))].total_return);
    result.return_p50 = static_cast<float>(all_details[std::max(0, static_cast<int>(0.50 * n))].total_return);
    result.return_p95 = static_cast<float>(all_details[std::min(n - 1, static_cast<int>(0.95 * n))].total_return);
    result.max_dd_p50 = static_cast<float>(all_details[std::max(0, static_cast<int>(0.50 * n))].max_drawdown);
    result.max_dd_p95 = static_cast<float>(all_details[std::min(n - 1, static_cast<int>(0.95 * n))].max_drawdown);

    double annualize_factor = static_cast<double>(_config.bars_per_year) / _nBars;
    result.median_annual_ret = static_cast<float>(
        std::pow(1.0 + all_details[std::max(0, static_cast<int>(0.50 * n))].total_return, annualize_factor) - 1.0
    );

    {
        // 最差 1% 平均回撤：按 max_drawdown 排序
        std::vector<double> sorted_dd;
        sorted_dd.reserve(n);
        for (const auto& d : all_details) sorted_dd.push_back(d.max_drawdown);
        std::sort(sorted_dd.begin(), sorted_dd.end());

        int tail_count = std::max(1, static_cast<int>(0.01 * n));
        double sum = 0.0;
        for (int i = 0; i < tail_count; ++i) sum += sorted_dd[i];
        result.tail_1pct_avg_dd = static_cast<float>(sum / tail_count);
    }

    result.method = method;
    result.block_size = block_size;
    result.autocorrelation = acf;
    result.n_simulations = n;

    return result;
}

void MonteCarloSimulator::aggregateStressResults(
    std::vector<PathResult>& paths,
    float& out_ruin_prob_high,
    float& out_ruin_prob_low,
    float& out_return_p5,
    float& out_return_p50,
    float& out_max_dd_p50,
    std::vector<PathDetail>& out_worst_paths,
    int worst_count
) {
    int n = static_cast<int>(paths.size());
    if (n == 0) {
        out_ruin_prob_high = 0.0f;
        out_ruin_prob_low = 0.0f;
        out_return_p5 = 0.0f;
        out_return_p50 = 0.0f;
        out_max_dd_p50 = 0.0f;
        return;
    }

    std::vector<PathDetail> details;
    details.reserve(n);
    for (const auto& p : paths) {
        details.push_back(buildDetail(p));
    }

    // 按总收益率排序
    std::sort(details.begin(), details.end(),
              [](const PathDetail& a, const PathDetail& b) { return a.total_return < b.total_return; });

    // 保存最差路径
    if (worst_count > 0) {
        int count = std::min(worst_count, n);
        out_worst_paths.assign(details.begin(), details.begin() + count);
    }

    int ruined_high = 0;
    int ruined_low = 0;
    for (const auto& d : details) {
        double high_threshold = _config.initial_capital * _ruinThresholdHigh;
        double low_threshold = _config.initial_capital * _ruinThresholdLow;
        if ((_config.initial_capital + d.total_return) < high_threshold) ruined_high++;
        if ((_config.initial_capital + d.total_return) < low_threshold) ruined_low++;
    }

    out_ruin_prob_high = static_cast<float>(ruined_high) / n;
    out_ruin_prob_low = static_cast<float>(ruined_low) / n;
    out_return_p5 = static_cast<float>(details[std::max(0, static_cast<int>(0.05 * n))].total_return);
    out_return_p50 = static_cast<float>(details[std::max(0, static_cast<int>(0.50 * n))].total_return);
    out_max_dd_p50 = static_cast<float>(details[std::max(0, static_cast<int>(0.50 * n))].max_drawdown);
}

// ============================================================
// 辅助函数
// ============================================================

double MonteCarloSimulator::computeAutocorrelation(const std::vector<double>& returns, int lag) {
    int n = static_cast<int>(returns.size());
    if (n <= lag || n < 2) {
        return 0.0;
    }

    double mean = 0.0;
    for (double r : returns) {
        mean += r;
    }
    mean /= n;

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

int MonteCarloSimulator::computeBlockSize(int n_bars) const {
    int target_blocks = std::max(20, _config.bars_per_year / 252 * 25);
    int blk = std::max(5, std::min(n_bars / 3, n_bars / target_blocks * 10));
    return blk;
}

double MonteCarloSimulator::computeOriginalStd() const {
    if (_nBars < 2) return 0.0;

    double mean = 0.0;
    for (double r : _returns) mean += r;
    mean /= _nBars;

    double var = 0.0;
    for (double r : _returns) {
        double d = r - mean;
        var += d * d;
    }
    return std::sqrt(var / (_nBars - 1));
}

// ============================================================
// 格式化输出
// ============================================================

std::string McResult::toString() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);

    ss << "=== Monte Carlo Bootstrap Analysis ===\n";
    ss << "  Method:             " << (method == 0 ? "Standard Bootstrap" : "Block Bootstrap") << "\n";
    ss << "  Block Size:         " << block_size << "\n";
    ss << "  Autocorrelation:    " << autocorrelation << "\n";
    ss << "  Simulations:        " << n_simulations << "\n";
    ss << "\n";
    ss << "--- Normal Scenario ---\n";
    ss << "  Ruin Prob (<50%):   " << (ruin_prob_high * 100.0f) << "%\n";
    ss << "  Ruin Prob (<30%):   " << (ruin_prob_low * 100.0f) << "%\n";
    ss << "  Return P5:          " << (return_p5 * 100.0f) << "%\n";
    ss << "  Return P50:         " << (return_p50 * 100.0f) << "%\n";
    ss << "  Return P95:         " << (return_p95 * 100.0f) << "%\n";
    ss << "  MaxDD P50:          " << (max_dd_p50 * 100.0f) << "%\n";
    ss << "  MaxDD P95:          " << (max_dd_p95 * 100.0f) << "%\n";
    ss << "  Median Annual Ret:  " << (median_annual_ret * 100.0f) << "%\n";
    ss << "  Tail 1% Avg DD:     " << (tail_1pct_avg_dd * 100.0f) << "%\n";

    if (!worst_paths.empty()) {
        ss << "\n--- Worst " << worst_paths.size() << " Paths (Failure Analysis) ---\n";
        int show = std::min(5, static_cast<int>(worst_paths.size()));
        for (int i = 0; i < show; ++i) {
            const auto& p = worst_paths[i];
            ss << "  #" << (i+1) << " ret=" << (p.total_return * 100.0f) << "%"
               << " maxDD=" << (p.max_drawdown * 100.0f) << "%"
               << " winRate=" << (p.win_rate * 100.0f) << "%"
               << " longestLoss=" << p.longest_loss_streak
               << " volRatio=" << p.vol_ratio << "\n";
        }
    }

    if (!best_paths.empty()) {
        ss << "\n--- Best " << best_paths.size() << " Paths (Success Analysis) ---\n";
        int show = std::min(5, static_cast<int>(best_paths.size()));
        for (int i = 0; i < show; ++i) {
            const auto& p = best_paths[i];
            ss << "  #" << (i+1) << " ret=" << (p.total_return * 100.0f) << "%"
               << " maxDD=" << (p.max_drawdown * 100.0f) << "%"
               << " winRate=" << (p.win_rate * 100.0f) << "%"
               << " longestWin=" << p.longest_win_streak
               << " volRatio=" << p.vol_ratio << "\n";
        }
    }

    ss << "\n--- Stress Test (Vol × 1.5) ---\n";
    ss << "  Ruin Prob (<50%):   " << (stress_ruin_prob_high * 100.0f) << "%\n";
    ss << "  Return P5:          " << (stress_return_p5 * 100.0f) << "%\n";
    ss << "  MaxDD P50:          " << (stress_max_dd_p50 * 100.0f) << "%\n";

    ss << "\n--- Liquidity Stress (Tail × 0.8) ---\n";
    ss << "  Ruin Prob (<50%):   " << (liq_stress_ruin_prob_high * 100.0f) << "%\n";
    ss << "  Return P5:          " << (liq_stress_return_p5 * 100.0f) << "%\n";
    ss << "  MaxDD P50:          " << (liq_stress_max_dd_p50 * 100.0f) << "%\n";

    ss << "\n--- Vol Clustering Stress (2× Block) ---\n";
    ss << "  Ruin Prob (<50%):   " << (vol_cluster_stress_ruin_prob_high * 100.0f) << "%\n";
    ss << "  Return P5:          " << (vol_cluster_stress_return_p5 * 100.0f) << "%\n";
    ss << "  MaxDD P50:          " << (vol_cluster_stress_max_dd_p50 * 100.0f) << "%\n";

    return ss.str();
}
