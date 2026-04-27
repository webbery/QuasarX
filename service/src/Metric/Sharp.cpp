#include "Metric/Sharp.h"
#include "Util/system.h"
#include "Bridge/exchange.h"
#include <algorithm>

namespace {
    // 根据交易报告更新持仓（单一整数模型，与 Python 一致）
    void update_position(std::map<symbol_t, int>& positions,
                         symbol_t symbol,
                         const TradeReport& report) {
        if (report._side == 0) {  // 买入 → 增加持仓
            positions[symbol] += report._quantity;
        } else {  // 卖出 → 减少持仓
            positions[symbol] -= report._quantity;
            if (positions[symbol] < 0) positions[symbol] = 0;  // 防止负持仓
        }
    }

    // 安全获取价格：索引越界时返回最后一个已知价格
    double get_safe_price(const Vector<double>& prices, size_t idx) {
        if (prices.empty()) {
            return 0.0;
        }
        if (idx >= prices.size()) {
            return prices.back();  // 向前填充
        }
        return prices[idx];
    }
}

float sharp_ratio(const crash_flow_t& flow, const DataContext& context, double freerate) {
    const double ANNUAL_RISK_FREE_RATE = freerate;

    Set<symbol_t> symbols;
    Map<symbol_t, List<TradeReport>> reports;
    auto& times = context.GetTime();
    if (times.empty()) {
        WARN("[Sharp] times is empty, return 0");
        return 0.0f;
    }

    // 1. 组织交易记录
    for (auto& item: flow) {
        symbols.insert(item.first);
        reports[item.first].emplace_back(item.second);
    }

    INFO("[Sharp] ====== Sharp Ratio Calculation Start ======");
    INFO("[Sharp] cash_flow entries: {}", flow.size());
    INFO("[Sharp] unique symbols: {}", symbols.size());
    INFO("[Sharp] time axis length: {}", times.size());
    INFO("[Sharp] initialCapital: {}", context.getInitialCapital());

    // 2. 准备价格数据
    std::map<symbol_t, Vector<double>> price_data;
    int price_fail_count = 0;
    for (auto& symbol : symbols) {
        String name = get_symbol(symbol);
        String key = name + ".close";
        try {
            auto& closes = context.get<Vector<double>>(key);
            price_data[symbol] = closes;
        } catch (...) {
            price_fail_count++;
            WARN("[Sharp] Failed to get price data for symbol: {}", name);
            continue;
        }
    }
    INFO("[Sharp] price data loaded: success={}, failed={}", price_data.size(), price_fail_count);

    // 打印每个 symbol 的价格数组长度
    for (const auto& [symbol, prices] : price_data) {
        String name = get_symbol(symbol);
        INFO("[Sharp] Symbol {} price data size: {}", name, prices.size());
    }

    // 3. 构建时间到交易的映射
    std::map<time_t, std::vector<std::pair<symbol_t, TradeReport>>> trades_by_time;
    for (const auto& [symbol, report_list] : reports) {
        for (const auto& report : report_list) {
            trades_by_time[report._time].push_back({symbol, report});
        }
    }

    // 4. 逐日计算组合价值（与 Python 逻辑一致）
    double cash = context.getInitialCapital();
    std::map<symbol_t, int> positions;  // 单一整数持仓（正数=多头，0=空仓）
    Vector<double> portfolio_values;
    int trade_day_count = 0;

    size_t day_idx = 0;
    for (auto itr = times.begin(); itr != times.end(); ++day_idx, ++itr) {
        time_t current_time = *itr;

        // 处理当日交易
        if (trades_by_time.find(current_time) != trades_by_time.end()) {
            trade_day_count++;
            for (const auto& [symbol, report] : trades_by_time[current_time]) {
                update_position(positions, symbol, report);
                // 更新现金：买入减少，卖出增加
                if (report._side == 0) {  // 买入
                    cash -= report._trade_amount;
                } else {  // 卖出
                    cash += report._trade_amount;
                }
            }
        }

        // 计算当日组合价值 = 现金 + 持仓市值
        double market_value = 0.0;
        for (const auto& [symbol, qty] : positions) {
            if (qty <= 0) continue;  // 空仓跳过

            auto it = price_data.find(symbol);
            if (it == price_data.end() || it->second.empty()) continue;

            // 使用安全价格获取（越界时用最后一个价格）
            double price = get_safe_price(it->second, day_idx);
            market_value += qty * price;
        }

        double portfolio_value = cash + market_value;
        portfolio_values.push_back(portfolio_value);
    }

    INFO("[Sharp] trade days with cash flow: {}", trade_day_count);
    INFO("[Sharp] portfolio_values count: {}", portfolio_values.size());
    if (!portfolio_values.empty()) {
        INFO("[Sharp] initial portfolio value: {:.2f}", portfolio_values.front());
        INFO("[Sharp] final portfolio value: {:.2f}, final cash: {:.2f}", 
             portfolio_values.back(), cash);
    }

    // 5. 计算日收益率序列
    Vector<double> daily_returns;
    int abnormal_return_count = 0;
    for (size_t i = 1; i < portfolio_values.size(); i++) {
        double prev_value = portfolio_values[i - 1];
        double curr_value = portfolio_values[i];
        if (prev_value != 0) {
            double daily_return = (curr_value - prev_value) / prev_value;
            // 检测异常收益率（超过 50%）
            if (std::abs(daily_return) > 0.5) {
                abnormal_return_count++;
                WARN("[Sharp] Abnormal return on day {}: {:.4f} ({:.2f}%), prev={:.2f}, curr={:.2f}",
                     i, daily_return, daily_return * 100, prev_value, curr_value);
            }
            daily_returns.push_back(daily_return);
        }
    }

    INFO("[Sharp] daily_returns count: {}", daily_returns.size());
    INFO("[Sharp] abnormal return count (>50%): {}", abnormal_return_count);

    if (daily_returns.size() < 2) {
        WARN("[Sharp] daily_returns size < 2 ({}), return 0", daily_returns.size());
        return 0.0f;
    }

    // 6. 计算夏普比率（与 Python 一致：算术平均年化）
    double sum_returns = 0.0;
    for (double ret : daily_returns) {
        sum_returns += ret;
    }
    double mean_daily_return = sum_returns / daily_returns.size();

    double sum_squared_diffs = 0.0;
    for (double ret : daily_returns) {
        double diff = ret - mean_daily_return;
        sum_squared_diffs += diff * diff;
    }
    double std_daily_return = std::sqrt(sum_squared_diffs / (daily_returns.size() - 1));

    // 年化
    double annualized_return = mean_daily_return * YEAR_DAY;
    double annualized_volatility = std_daily_return * std::sqrt(static_cast<double>(YEAR_DAY));

    INFO("[Sharp] mean_daily_return: {:.8f}, std_daily_return: {:.8f}", 
         mean_daily_return, std_daily_return);
    INFO("[Sharp] annualized_return: {:.6f} ({:.4f}%), annualized_volatility: {:.6f} ({:.4f}%)", 
         annualized_return, annualized_return * 100, 
         annualized_volatility, annualized_volatility * 100);

    if (annualized_volatility <= 0) {
        WARN("[Sharp] annualized_volatility <= 0 ({:.8f}), return 0", annualized_volatility);
        return 0.0f;
    }

    float result = static_cast<float>((annualized_return - ANNUAL_RISK_FREE_RATE) / annualized_volatility);
    INFO("[Sharp] ====== Sharp Ratio Result: {:.6f} ======", result);
    return result;
}
