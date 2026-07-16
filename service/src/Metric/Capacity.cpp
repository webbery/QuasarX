#include "Metric/Capacity.h"
#include "Util/data.h"
#include "Util/system.h"
#include <cmath>
#include <algorithm>
#include <numeric>

// ─── 快速重放扫描 ─────────────────────────────────────────────

Vector<CapacityPoint> Capacity::scan(
    const Vector<CapacityTrade>& trades,
    const Map<symbol_t, Vector<double>>& adv_data,
    const Map<symbol_t, Vector<double>>& vol_data,
    double base_capital,
    double min_capital,
    double max_capital,
    int steps,
    double eta,
    double max_participation)
{
    if (trades.empty() || base_capital <= 0) {
        return {};
    }

    // 计算总天数（从交易日志推断）
    size_t total_days = 0;
    for (const auto& t : trades) {
        total_days = std::max(total_days, t.day_index + 1);
    }
    // 也从 adv_data 推断
    for (const auto& [sym, adv] : adv_data) {
        total_days = std::max(total_days, adv.size());
    }

    // 对数均匀生成资金量序列
    double log_min = std::log(std::max(min_capital, 1.0));
    double log_max = std::log(std::max(max_capital, min_capital + 1.0));
    double log_step = (log_max - log_min) / std::max(steps - 1, 1);

    Vector<CapacityPoint> curve;
    curve.reserve(steps);

    double baseline_sharpe = 0;

    for (int i = 0; i < steps; ++i) {
        double capital = std::exp(log_min + i * log_step);

        double avg_part = 0, max_part = 0, avg_slippage_bps = 0;
        int orders_above = 0;

        auto daily_returns = replayWithImpact(
            trades, adv_data, vol_data,
            base_capital, capital, eta, total_days,
            max_participation,
            avg_part, max_part, avg_slippage_bps, orders_above
        );

        CapacityPoint point;
        point.capital = capital;
        point.avg_participation = avg_part;
        point.max_participation = max_part;
        point.avg_slippage_bps = avg_slippage_bps;
        point.orders_above_limit = orders_above;

        computeMetrics(daily_returns, point);

        if (i == 0) {
            baseline_sharpe = point.sharpe;
        }
        point.sharpe_decay = (baseline_sharpe > 1e-6)
            ? (baseline_sharpe - point.sharpe) / baseline_sharpe
            : 0.0;

        curve.push_back(point);
    }

    return curve;
}

// ─── 单资金量快速重放 ─────────────────────────────────────────

Vector<double> Capacity::replayWithImpact(
    const Vector<CapacityTrade>& trades,
    const Map<symbol_t, Vector<double>>& adv_data,
    const Map<symbol_t, Vector<double>>& vol_data,
    double base_capital,
    double target_capital,
    double eta,
    size_t total_days,
    double max_participation,
    double& out_avg_part,
    double& out_max_part,
    double& out_avg_slippage_bps,
    int& out_orders_above)
{
    Vector<double> daily_returns(total_days, 0.0);
    double scale = target_capital / base_capital;

    // 按标的分组，配对买卖计算 round-trip
    Map<symbol_t, Vector<const CapacityTrade*>> by_symbol;
    for (const auto& t : trades) {
        by_symbol[t.symbol].push_back(&t);
    }

    double sum_part = 0;
    double max_part_val = 0;
    double sum_slippage = 0;
    int trade_count = 0;
    int above_count = 0;

    for (auto& [symbol, sym_trades] : by_symbol) {
        // 按 day_index 排序
        std::sort(sym_trades.begin(), sym_trades.end(),
            [](const auto* a, const auto* b) { return a->day_index < b->day_index; });

        const auto& adv_series = adv_data.count(symbol) ? adv_data.at(symbol) : Vector<double>{};
        const auto& vol_series = vol_data.count(symbol) ? vol_data.at(symbol) : Vector<double>{};

        // 配对买卖
        bool in_position = false;
        double entry_price = 0;
        size_t entry_day = 0;
        int64_t entry_shares = 0;

        for (const auto* trade : sym_trades) {
            int64_t adjusted_shares = static_cast<int64_t>(trade->shares * scale);
            adjusted_shares = (adjusted_shares / 100) * 100;
            if (adjusted_shares <= 0) continue;

            // 查当日 ADV 和波动率
            double adv = 0, sigma = 0;
            if (trade->day_index < adv_series.size()) {
                adv = adv_series[trade->day_index];
            }
            if (trade->day_index < vol_series.size()) {
                sigma = vol_series[trade->day_index];
            }

            double participation = (adv > 0) ? static_cast<double>(adjusted_shares) / adv : 0;
            double slippage = computeSlippage(sigma, participation, eta);

            sum_part += participation;
            max_part_val = std::max(max_part_val, participation);
            sum_slippage += slippage;
            trade_count++;
            if (participation > max_participation) {
                above_count++;
            }

            if (trade->side == 0) {
                // BUY
                entry_price = trade->price * (1.0 + slippage);
                entry_day = trade->day_index;
                entry_shares = adjusted_shares;
                in_position = true;
            } else if (in_position && trade->side == 1) {
                // SELL — 完成 round-trip
                double exit_price = trade->price * (1.0 - slippage);
                double pnl = (exit_price - entry_price) * entry_shares;

                // 将 PnL 分配到持仓期间的每日
                size_t hold_days = trade->day_index - entry_day;
                if (hold_days > 0 && entry_price > 0) {
                    // 简化：将收益均匀分配到持仓期
                    double daily_pnl_ratio = (exit_price / entry_price - 1.0) / hold_days;
                    for (size_t d = entry_day + 1; d <= trade->day_index && d < total_days; ++d) {
                        daily_returns[d] += daily_pnl_ratio;
                    }
                } else if (entry_price > 0) {
                    // 同日进出
                    daily_returns[trade->day_index] += (exit_price / entry_price - 1.0);
                }

                in_position = false;
            }
        }
    }

    out_avg_part = (trade_count > 0) ? sum_part / trade_count : 0;
    out_max_part = max_part_val;
    out_avg_slippage_bps = (trade_count > 0) ? sum_slippage / trade_count * 10000.0 : 0;
    out_orders_above = above_count;

    return daily_returns;
}

// ─── 指标计算 ──────────────────────────────────────────────────

void Capacity::computeMetrics(const Vector<double>& daily_returns, CapacityPoint& point) {
    if (daily_returns.empty()) return;

    // 累计收益
    double cum = 1.0;
    double peak = 1.0;
    double max_dd = 0;
    int win_days = 0;
    int trade_days = 0;

    Vector<double> cum_returns;
    cum_returns.reserve(daily_returns.size());

    for (double r : daily_returns) {
        cum *= (1.0 + r);
        cum_returns.push_back(cum);
        if (cum > peak) peak = cum;
        double dd = (peak > 0) ? (cum - peak) / peak : 0;
        if (dd < max_dd) max_dd = dd;
        if (r != 0) {
            trade_days++;
            if (r > 0) win_days++;
        }
    }

    point.total_return = cum - 1.0;
    point.max_drawdown = max_dd;
    point.win_rate = (trade_days > 0) ? static_cast<double>(win_days) / trade_days : 0;

    // Sharpe（年化）
    double sum = 0, sum2 = 0;
    int n = 0;
    for (double r : daily_returns) {
        if (r != 0) {
            sum += r;
            sum2 += r * r;
            n++;
        }
    }
    if (n > 1) {
        double mean = sum / n;
        double var = sum2 / n - mean * mean;
        double std_dev = std::sqrt(std::max(var, 0.0));
        if (std_dev > 1e-10) {
            point.sharpe = (mean / std_dev) * std::sqrt(252.0);
        }
    }
}

// ─── 摘要计算 ──────────────────────────────────────────────────

CapacitySummary Capacity::computeSummary(
    const Vector<CapacityPoint>& curve,
    const Map<symbol_t, Vector<double>>& adv_data,
    double baseline_sharpe)
{
    CapacitySummary summary;

    // 找 Sharpe 衰减 20% 和 50% 对应的资金量（线性插值）
    for (size_t i = 1; i < curve.size(); ++i) {
        if (summary.capacity_20pct == 0 && curve[i].sharpe_decay >= 0.20) {
            // 线性插值
            double ratio = (0.20 - curve[i-1].sharpe_decay) /
                           (curve[i].sharpe_decay - curve[i-1].sharpe_decay + 1e-10);
            summary.capacity_20pct = curve[i-1].capital +
                ratio * (curve[i].capital - curve[i-1].capital);
        }
        if (summary.capacity_50pct == 0 && curve[i].sharpe_decay >= 0.50) {
            double ratio = (0.50 - curve[i-1].sharpe_decay) /
                           (curve[i].sharpe_decay - curve[i-1].sharpe_decay + 1e-10);
            summary.capacity_50pct = curve[i-1].capital +
                ratio * (curve[i].capital - curve[i-1].capital);
        }
    }

    // 找瓶颈标的（ADV 最小的标的）
    double min_adv = 1e18;
    for (const auto& [sym, adv] : adv_data) {
        double avg_adv = 0;
        int count = 0;
        for (double v : adv) {
            if (v > 0) { avg_adv += v; count++; }
        }
        if (count > 0) avg_adv /= count;
        if (avg_adv < min_adv && avg_adv > 0) {
            min_adv = avg_adv;
            summary.bottleneck_symbol = get_symbol(sym);
            summary.bottleneck_adv = avg_adv;
        }
    }

    return summary;
}

// ─── 加载市场数据 ──────────────────────────────────────────────

bool Capacity::loadMarketData(
    const Vector<symbol_t>& symbols,
    int adv_window,
    Map<symbol_t, Vector<double>>& out_adv,
    Map<symbol_t, Vector<double>>& out_vol)
{
    for (const auto& sym : symbols) {
        String sym_str = get_symbol(sym);

        // 加载 volume 和 close
        auto data = LoadHistoryData(sym_str, {"close", "volume"});
        if (data.empty() || !data.count("close") || !data.count("volume")) {
            WARN("[Capacity] Failed to load data for {}", sym_str);
            return false;
        }

        const auto& close = data.at("close");
        const auto& volume = data.at("volume");
        size_t n = close.size();

        // 计算 ADV（rolling mean of volume）
        Vector<double> adv(n, 0);
        for (size_t i = 0; i < n; ++i) {
            size_t start = (i >= static_cast<size_t>(adv_window)) ? i - adv_window + 1 : 0;
            double sum = 0;
            int count = 0;
            for (size_t j = start; j <= i; ++j) {
                sum += volume[j];
                count++;
            }
            adv[i] = (count > 0) ? sum / count : 0;
        }

        // 计算日波动率（rolling std of daily returns）
        Vector<double> vol(n, 0);
        for (size_t i = 1; i < n; ++i) {
            size_t start = (i >= static_cast<size_t>(adv_window)) ? i - adv_window + 1 : 1;
            double sum = 0, sum2 = 0;
            int count = 0;
            for (size_t j = start; j <= i; ++j) {
                double ret = (close[j - 1] > 0) ? (close[j] - close[j-1]) / close[j-1] : 0;
                sum += ret;
                sum2 += ret * ret;
                count++;
            }
            if (count > 1) {
                double mean = sum / count;
                double var = sum2 / count - mean * mean;
                vol[i] = std::sqrt(std::max(var, 0.0)) * std::sqrt(252.0);
            }
        }

        out_adv[sym] = std::move(adv);
        out_vol[sym] = std::move(vol);
    }

    return true;
}
