#include "std_header.h"
#include "Metric/Return.h"
#include "Util/system.h"
#include "Bridge/exchange.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "server.h"

namespace {
    // 多空分离持仓模型
    struct Pos { int long_qty = 0; int short_qty = 0; };

    // 多空分离持仓模型
    struct _Position {
        int long_qty = 0;   // 多头持仓
        int short_qty = 0;  // 空头持仓

        bool empty() const { return long_qty == 0 && short_qty == 0; }
    };

    // 根据交易报告更新持仓（多空分离模型）
    void update_position(std::map<symbol_t, _Position>& positions,
                         symbol_t symbol,
                         const TradeReport& report) {
        auto& pos = positions[symbol];
        int qty = report._quantity;

        if (report._side == 0) {  // 买入
            if (report._flag == 0) {
                // 买入开多
                pos.long_qty += qty;
            } else {
                // 买入平空
                pos.short_qty -= qty;
                if (pos.short_qty < 0) pos.short_qty = 0;
            }
        } else {  // 卖出
            if (report._flag == 0) {
                // 卖出开空
                pos.short_qty += qty;
            } else {
                // 卖出平多
                pos.long_qty -= qty;
                if (pos.long_qty < 0) pos.long_qty = 0;
            }
        }
    }

    // 计算几何平均数
    double geometric_mean(const std::vector<double>& returns) {
        if (returns.empty()) return 1.0;

        double product = 1.0;
        int count = 0;
        for (double ret : returns) {
            if (ret > -1.0) {  // 收益率不能小于-100%
                product *= (1.0 + ret);
                count++;
            }
        }

        if (count == 0) return 1.0;
        return std::pow(product, 1.0 / count) - 1.0;
    }

    // 计算滚动年化收益率
    double rolling_annualized_return(const std::vector<double>& daily_returns,
                                     int window_days,
                                     int index) {
        if (index < window_days - 1 || daily_returns.empty()) {
            return 0.0;
        }

        int start_idx = std::max(0, static_cast<int>(index) - window_days + 1);
        int count = 0;
        double product = 1.0;

        for (int i = start_idx; i <= static_cast<int>(index); ++i) {
            if (i >= 0 && i < static_cast<int>(daily_returns.size())) {
                double ret = daily_returns[i];
                if (ret > -1.0) {
                    product *= (1.0 + ret);
                    count++;
                }
            }
        }

        if (count == 0 || product <= 0.0) {
            return 0.0;
        }

        // 计算窗口内的累计收益率
        double cumulative_return = product - 1.0;

        // 年化处理
        double years = static_cast<double>(count) / YEAR_DAY;  // 假设252个交易日
        if (years > 0) {
            return std::pow(1.0 + cumulative_return, 1.0 / years) - 1.0;
        }

        return 0.0;
    }

    // 计算从起始点到当前点的年化收益率
    double cumulative_annualized_return(const std::vector<double>& daily_returns, int current_index) {
        if (current_index < 0 || daily_returns.empty()) {
            return 0.0;
        }

        double product = 1.0;
        int count = 0;

        for (int i = 0; i <= current_index && i < static_cast<int>(daily_returns.size()); ++i) {
            double ret = daily_returns[i];
            if (ret > -1.0) {
                product *= (1.0 + ret);
                count++;
            }
        }

        if (count == 0 || product <= 0.0) {
            return 0.0;
        }

        double cumulative_return = product - 1.0;
        double years = static_cast<double>(count) / YEAR_DAY;  // 假设252个交易日

        if (years > 0) {
            return std::pow(1.0 + cumulative_return, 1.0 / years) - 1.0;
        }

        return 0.0;
    }

    // 计算每日收益率序列（修正简单收益率法）
    // 公式: R_t = (V_t - V_{t-1} - CF_t) / V_{t-1}
    // 其中 CF_t = 当日外部现金流（买入=负流出, 卖出=正流入）
    std::vector<double> simple_daily_returns(const std::vector<double>& daily_values,
                                               const std::vector<double>& daily_cash_flows) {
        std::vector<double> daily_returns;
        daily_returns.reserve(daily_values.size() > 0 ? daily_values.size() - 1 : 0);

        for (size_t i = 1; i < daily_values.size(); ++i) {
            double prev_value = daily_values[i - 1];
            double curr_value = daily_values[i];
            double cash_flow = daily_cash_flows[i];

            if (prev_value != 0.0) {
                double daily_return = (curr_value - prev_value - cash_flow) / prev_value;
                daily_returns.push_back(daily_return);
            } else {
                daily_returns.push_back(0.0);
            }
        }

        return daily_returns;
    }

}

float annual_return_ratio(const crash_flow_t& flow, const DataContext& context, int mode, int window) {
    std::vector<double> annual_returns;

    // 收集所有symbol
    std::set<symbol_t> symbols;
    for (auto& item : flow) {
        symbols.insert(item.first);
    }

    // 获取时间轴
    auto& times = context.GetTime();
    if (times.empty()) {
        return 0;
    }

    // 1. 计算每日组合价值和现金流
    std::vector<double> daily_values(times.size(), 0.0);
    std::vector<double> daily_cash_flows(times.size(), 0.0);

    // 持仓记录：symbol -> 持仓（多空分离）
    std::map<symbol_t, _Position> positions;

    // 价格数据缓存
    std::map<symbol_t, std::vector<double>> price_data;

    // 加载价格数据
    for (auto symbol : symbols) {
        String name = get_symbol(symbol);
        String key = name + ".close";
        try {
            auto& closes = context.get<Vector<double>>(key);
            price_data[symbol] = closes;
        } catch (...) {
            continue;
        }
    }

    // 按时间顺序处理交易记录
    // 首先将交易记录按时间分组
    std::map<time_t, std::vector<std::pair<symbol_t, TradeReport>>> trades_by_time;

    for (const auto& [symbol, report] : flow) {
        trades_by_time[report._time].push_back({symbol, report});
    }

    // 遍历每个交易日
    size_t i = 0;
    for (auto itr = times.begin(); itr != times.end(); ++i, ++itr) {
        time_t current_time = *itr;

        // 处理当前时间点的交易
        if (trades_by_time.find(current_time) != trades_by_time.end()) {
            for (const auto& [symbol, report] : trades_by_time[current_time]) {
                // 更新持仓
                update_position(positions, symbol, report);

                // 现金流：买入=流出(负)，卖出=流入(正)
                if (report._side == 0) {  // 买入
                    daily_cash_flows[i] -= report._trade_amount;
                } else {  // 卖出
                    daily_cash_flows[i] += report._trade_amount;
                }
            }
        }

        // 计算当前组合价值（多空分离）
        double portfolio_value = 0.0;
        for (const auto& [symbol, pos] : positions) {
            if (pos.empty()) continue;

            auto it = price_data.find(symbol);
            if (it == price_data.end()) continue;

            const auto& closes = it->second;
            if (i < closes.size()) {
                // 多头市值为正，空头市值为负
                portfolio_value += pos.long_qty * closes[i] - pos.short_qty * closes[i];
            }
        }

        // 加上现金部分（如果有现金账户数据）
        // 这里可以扩展：从context获取现金余额
        daily_values[i] = portfolio_value;
    }

    // 2. 计算每日收益率序列
    std::vector<double> daily_returns = simple_daily_returns(daily_values, daily_cash_flows);

    // 3. 根据模式计算动态年化回报率
    annual_returns.reserve(times.size());

    switch (mode) {
        case 0: {  // 累计年化收益率
            // 第一个时间点设为0
            annual_returns.push_back(0.0);

            for (size_t i = 1; i < times.size(); ++i) {
                double annual_return = 0.0;

                if (i <= daily_returns.size()) {
                    int current_idx = static_cast<int>(i) - 1;
                    annual_return = cumulative_annualized_return(daily_returns, current_idx);
                }

                annual_returns.push_back(annual_return);
            }
            break;
        }

        case 1: {  // 滚动年化收益率（固定窗口）
            // 第一个时间点设为0
            annual_returns.push_back(0.0);

            for (size_t i = 1; i < times.size(); ++i) {
                double annual_return = 0.0;

                if (i <= daily_returns.size()) {
                    int current_idx = static_cast<int>(i) - 1;
                    annual_return = rolling_annualized_return(daily_returns, window, current_idx);
                }

                annual_returns.push_back(annual_return);
            }
            break;
        }

        case 2: {  // 移动平均年化收益率
            // 第一个时间点设为0
            annual_returns.push_back(0.0);

            for (size_t i = 1; i < times.size(); ++i) {
                double annual_return = 0.0;

                if (i <= daily_returns.size()) {
                    // 计算到当前点的年化
                    int current_idx = static_cast<int>(i) - 1;
                    annual_return = cumulative_annualized_return(daily_returns, current_idx);

                    // 对最近window个点的年化值进行平均
                    if (static_cast<int>(i) >= window) {
                        double sum = 0.0;
                        int count = 0;

                        for (int j = std::max(0, static_cast<int>(i) - window); j < static_cast<int>(i); ++j) {
                            double past_annual = cumulative_annualized_return(daily_returns,
                                                                             std::min(j - 1,
                                                                                     static_cast<int>(daily_returns.size()) - 1));
                            sum += past_annual;
                            count++;
                        }

                        if (count > 0) {
                            annual_return = sum / count;
                        }
                    }
                }

                annual_returns.push_back(annual_return);
            }
            break;
        }

        default:
            // 默认模式0
            annual_returns.push_back(0.0);
            for (size_t i = 1; i < times.size(); ++i) {
                annual_returns.push_back(0.0);
            }
    }

    return annual_returns.back();
}

// 计算总收益率（回测简化版：无外部资金进出）
// 公式: total_return = (V_end - V_start) / V_start
// 适用场景: 回测中初始资金一次性投入，期间无外部资金进出
// 参数: daily_values - 每日组合价值序列（持仓市值）
//       initial_capital - 初始投入本金
double simple_total_return(const std::vector<double>& daily_values, double initial_capital) {
    if (daily_values.empty() || initial_capital <= 0.0) {
        return 0.0;
    }

    double final_value = daily_values.back();
    return (final_value - initial_capital) / initial_capital;
}

// 从每日收益率序列计算年化收益率
// 公式: annualized = (1 + total_return)^(1/years) - 1
// 参数: daily_returns - 每日收益率序列
//       count - 有效交易日数（用于计算 years = count / YEAR_DAY）
float compute_annualized_return(double total_return, int count) {
    double years = static_cast<double>(count) / YEAR_DAY;
    if (years > 0 && total_return > -1.0) {
        return static_cast<float>(std::pow(1.0 + total_return, 1.0 / years) - 1.0);
    }

    return 0.0f;
}

Vector<double> simple_daily_return(const Vector<double>& daily_values) {
    // 计算每日收益率: R_t = (V_t - V_{t-1}) / V_{t-1}
    size_t cnt = (int)daily_values.size() - 1;
    Vector<double> rets(cnt);
    for (size_t i = 0; i < cnt; ++i) {
        double prev = daily_values[i];
        double curr = daily_values[i + 1];
        // double cf = daily_cash_flows[i];
        if (prev != 0.0) {
            auto r = (curr - prev) / prev;
            rets[i] = r;
        } else {
            rets[i] = 0.0;
        }
    }
    return rets;
}

// 计算每日投资组合价值（现金 + 持仓市值）
// 返回: {daily_values, daily_cash_flows}
// 
// 注意：
// - 现金流使用交易报告中的原始金额（基于原始价格）
// - 持仓市值使用后复权价格评估，避免除权缺口导致的收益率跳变
// ========== 调整系数法计算组合价值 ==========
//
// 【问题背景】
// 回测中使用后复权价格评估持仓市值可以避免除权缺口导致的收益率跳变，
// 但后复权价格远高于原始价格（如 sz.000001 约 123 倍），导致组合价值
// 在买入/卖出瞬间发生数量级跳变。
//
// 【解决方案：调整系数法】
// 将后复权价格归一化到原始价格体系，公式：
//   调整后持仓市值 = Σ(持仓数量 × 后复权价格 / 调整系数)
//   组合总价值 = 现金 + 调整后持仓市值
//
// 【调整系数计算原理】
//   adjustment_ratio = mean(后复权价格 / 原始价格)
// 即在回测期间的所有交易日上，计算后复权价格与原始价格的比值，取平均值。
// 这个比值反映了该股票上市以来的累积复权效应，相对稳定（标准差通常 < 1%）。
//
// 【为什么这样做】
// 1. 保持后复权价格的收益率连续性（无除权缺口）
// 2. 使持仓市值与现金处于同一数量级（原始价格体系）
// 3. 买入/卖出瞬间组合价值平滑，不会产生虚假的收益率跳变
// ============================================================

std::pair<std::vector<double>, std::vector<double>>
build_portfolio_values(const crash_flow_t& flow, const DataContext& context, Server* server) {
    auto& times = context.GetTime();
    if (times.empty()) {
        return {{}, {}};
    }

    std::vector<double> daily_values(times.size(), 0.0);
    std::vector<double> daily_cash_flows(times.size(), 0.0);

    // 持仓记录（多空分离）
    std::map<symbol_t, Pos> positions;

    // 收集所有涉及的 symbol
    std::set<symbol_t> all_symbols;
    for (const auto& [symbol, report] : flow) {
        all_symbols.insert(symbol);
    }

    // 交易记录按时间分组
    std::map<time_t, std::vector<std::pair<symbol_t, TradeReport>>> trades_by_time;
    for (const auto& [symbol, report] : flow) {
        trades_by_time[report._time].push_back({symbol, report});
    }

    // ---- 第 1 步：从 StockHistorySimulation 获取后复权收盘价序列 ----
    struct HFQData {
        std::vector<time_t> datetimes;
        std::vector<double> closes;
    };
    std::map<symbol_t, HFQData> hfq_data;

    if (server) {
        auto* exchange = dynamic_cast<StockHistorySimulation*>(server->GetAvaliableStockExchange());
        if (exchange) {
            for (auto symbol : all_symbols) {
                try {
                    auto [dts, closes] = exchange->GetHFQCloseData(symbol);
                    if (!dts.empty()) {
                        hfq_data[symbol] = {std::move(dts), std::move(closes)};
                    }
                } catch (...) {
                    // 如果获取失败，跳过该标的
                }
            }
        }
    }

    // ---- 第 2 步：计算调整系数 ----
    // adjustment_ratio[symbol] = avg(后复权价格 / 原始价格)
    // 通过 times[i] 匹配 context 中的原始价格和 HFQ 数据中的后复权价格
    std::map<symbol_t, double> adjustment_ratios;

    for (auto symbol : all_symbols) {
        auto hfq_it = hfq_data.find(symbol);
        if (hfq_it == hfq_data.end() || hfq_it->second.datetimes.empty()) {
            INFO("[AdjustRatio] Symbol {}: no HFQ data, skipping", get_symbol(symbol));
            continue;
        }

        const auto& hfq_dts = hfq_it->second.datetimes;
        const auto& hfq_closes = hfq_it->second.closes;

        // 调试：打印 HFQ 数据样本
        INFO("[AdjustRatio] Symbol {} HFQ: {} rows, first_ts={}, first_close={:.2f}, last_ts={}, last_close={:.2f}",
             get_symbol(symbol), hfq_dts.size(),
             hfq_dts.front(), hfq_closes.front(),
             hfq_dts.back(), hfq_closes.back());

        // 从 context 获取原始价格序列（QuoteNode 写入的 {symbol}.close）
        String name = get_symbol(symbol);
        String closeKey = name + ".close";

        try {
            const auto& orig_prices = context.get<Vector<double>>(closeKey);
            INFO("[AdjustRatio] Symbol {} orig_prices: {} rows, first={:.2f}, last={:.2f}",
                 get_symbol(symbol), orig_prices.size(),
                 orig_prices.empty() ? 0.0 : orig_prices.front(),
                 orig_prices.empty() ? 0.0 : orig_prices.back());

            double sum_ratio = 0.0;
            int count = 0;
            int nan_count = 0;

            // 遍历回测时间轴，在每个时间点上计算后复权/原始价格的比值
            size_t i = 0;
            for (auto itr = times.begin(); itr != times.end() && i < orig_prices.size(); ++itr, ++i) {
                time_t ts = *itr;
                double orig = orig_prices[i];
                if (orig <= 0.0 || std::isnan(orig)) continue;

                // 在 HFQ 数据中查找 <= ts 的最近后复权价格
                double hfq_price = 0.0;
                for (int j = (int)hfq_dts.size() - 1; j >= 0; --j) {
                    if (hfq_dts[j] <= ts) {
                        hfq_price = hfq_closes[j];
                        break;
                    }
                }

                if (hfq_price > 0.0 && !std::isnan(hfq_price)) {
                    sum_ratio += hfq_price / orig;
                    count++;
                    if (count <= 3) {
                        INFO("[AdjustRatio] Sample {} ts={} orig={:.2f} hfq={:.2f} ratio={:.4f}",
                             count, ts, orig, hfq_price, hfq_price / orig);
                    }
                } else {
                    if (std::isnan(hfq_price)) nan_count++;
                }
            }

            INFO("[AdjustRatio] Symbol {} sum_ratio={:.2f} count={} nan_count={}",
                 get_symbol(symbol), sum_ratio, count, nan_count);

            if (count > 0) {
                adjustment_ratios[symbol] = sum_ratio / count;
                INFO("[AdjustRatio] Symbol {} FINAL ratio={:.4f}",
                     get_symbol(symbol), adjustment_ratios[symbol]);
            } else {
                WARN("[AdjustRatio] Symbol {} NO valid samples, ratio=1.0", get_symbol(symbol));
            }
        } catch (const std::exception& e) {
            INFO("[AdjustRatio] Symbol {} exception: {}", get_symbol(symbol), e.what());
        } catch (...) {
            INFO("[AdjustRatio] Symbol {} unknown exception", get_symbol(symbol));
        }
    }

    // ---- 第 3 步：辅助函数 ----
    // 根据 timestamp 查找对应的后复权价格
    auto get_hfq_price = [&](const symbol_t& symbol, time_t ts) -> double {
        auto it = hfq_data.find(symbol);
        if (it == hfq_data.end() || it->second.datetimes.empty()) {
            return 0.0;
        }
        const auto& dts = it->second.datetimes;
        const auto& prices = it->second.closes;
        for (int j = (int)dts.size() - 1; j >= 0; --j) {
            if (dts[j] <= ts) {
                return prices[j];
            }
        }
        return prices.empty() ? 0.0 : prices[0];
    };

    // ---- 第 4 步：遍历每个交易日，计算组合价值 ----
    std::map<symbol_t, double> last_trade_prices;
    double initial_capital = context.getInitialCapital();
    double cash = initial_capital;

    size_t i = 0;
    for (auto itr = times.begin(); itr != times.end(); ++i, ++itr) {
        time_t current_time = *itr;

        // 处理当前时间点的交易
        if (trades_by_time.contains(current_time)) {
            for (const auto& [symbol, report] : trades_by_time[current_time]) {
                auto& pos = positions[symbol];
                int qty = report._quantity;
                if (report._side == 0) {  // 买入
                    if (report._flag == 0) pos.long_qty += qty;
                    else { pos.short_qty -= qty; if (pos.short_qty < 0) pos.short_qty = 0; }
                } else {  // 卖出
                    if (report._flag == 0) pos.short_qty += qty;
                    else { pos.long_qty -= qty; if (pos.long_qty < 0) pos.long_qty = 0; }
                }

                if (report._side == 0) {
                    daily_cash_flows[i] -= report._trade_amount;
                    cash -= report._trade_amount;
                } else {
                    daily_cash_flows[i] += report._trade_amount;
                    cash += report._trade_amount;
                }

                last_trade_prices[symbol] = report._price;
            }
        }

        // 组合价值 = 现金 + 持仓市值
        // 持仓市值使用调整系数归一化：pos_value = qty × hfq_price / adjustment_ratio
        double portfolio_value = cash;
        double total_position_value = 0.0;

        for (const auto& [symbol, pos] : positions) {
            if (pos.long_qty == 0 && pos.short_qty == 0) continue;

            double hfq_price = get_hfq_price(symbol, current_time);
            if (hfq_price > 0.0) {
                double ratio = 1.0;
                auto ratio_it = adjustment_ratios.find(symbol);
                if (ratio_it != adjustment_ratios.end()) {
                    ratio = ratio_it->second;
                }
                total_position_value += (pos.long_qty - pos.short_qty) * hfq_price / ratio;
            } else {
                // 回退：使用原始价格
                auto it = last_trade_prices.find(symbol);
                if (it != last_trade_prices.end()) {
                    total_position_value += (pos.long_qty - pos.short_qty) * it->second;
                }
            }
        }

        portfolio_value += total_position_value;
        daily_values[i] = portfolio_value;

        // 打印前15天的调试信息
        if (i < 15) {
            INFO("[BuildPortfolio] Day {} ({}) cash={:.0f} pos_value={:.0f} total={:.0f} cash_flow={:.0f}",
                 i + 1, current_time, cash, total_position_value, portfolio_value, daily_cash_flows[i]);
        }
    }

    return {daily_values, daily_cash_flows};
}
