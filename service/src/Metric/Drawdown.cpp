#include "Metric/Drawdown.h"
#include "Util/system.h"
#include "Bridge/exchange.h"
#include <algorithm>
#include <cmath>

namespace {
    // 计算每日投资组合价值
    std::pair<std::vector<double>, std::vector<double>>
    calculate_daily_values(const crash_flow_t& flow, const DataContext& context) {
        // 收集所有 symbol
        std::set<symbol_t> symbols;
        for (auto& item : flow) {
            symbols.insert(item.first);
        }

        // 获取时间轴
        auto& times = context.GetTime();
        if (times.empty()) {
            return {{}, {}};
        }

        // 1. 计算每日组合价值和现金流
        std::vector<double> daily_values(times.size(), 0.0);
        std::vector<double> daily_cash_flows(times.size(), 0.0);

        // 持仓记录：symbol -> 持仓数量
        std::map<symbol_t, int> positions;

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
        daily_cash_flows[0] = context.getAvailableCapital();
        for (auto itr = times.begin(); itr != times.end(); ++i, ++itr) {
            time_t current_time = *itr;

            if (i > 0) {
                daily_cash_flows[i] = daily_cash_flows[i - 1];
            }
            // 处理当前时间点的交易
            if (trades_by_time.find(current_time) != trades_by_time.end()) {
                for (const auto& [symbol, report] : trades_by_time[current_time]) {
                    // 更新持仓
                    if (report._side == 0) {  // 买入
                        positions[symbol] += report._quantity;
                        daily_cash_flows[i] -= report._trade_amount;  // 现金流出
                    } else if (report._side == 1) {  // 卖出
                        positions[symbol] -= report._quantity;
                        daily_cash_flows[i] += report._trade_amount;  // 现金流入
                    }
                }
            }
            // 计算当前组合价值
            double portfolio_value = 0.0;
            for (const auto& [symbol, quantity] : positions) {
                if (quantity == 0) continue;

                auto it = price_data.find(symbol);
                if (it == price_data.end()) continue;

                const auto& closes = it->second;
                if (i < closes.size()) {
                    portfolio_value += quantity * closes[i];
                }
            }

            daily_values[i] = portfolio_value;
        }

        return {daily_values, daily_cash_flows};
    }
}

float max_drawdown_ratio(const crash_flow_t& flow, const DataContext& context) {
    auto [daily_values, daily_cash_flows] = calculate_daily_values(flow, context);

    if (daily_values.empty()) {
        return 0.0f;
    }

    double max_value = 0.0;
    double max_drawdown = 0.0;

    for (size_t i = 0; i < daily_values.size(); ++i) {
        double value = daily_values[i];

        // 更新历史最高值
        if (value > max_value) {
            max_value = value;
        }

        // 计算当前回撤
        if (max_value > 0) {
            double drawdown = (max_value - value) / max_value;
            if (drawdown > max_drawdown) {
                max_drawdown = drawdown;
            }
        }
    }

    return static_cast<float>(max_drawdown);
}

float total_return_ratio(const crash_flow_t& flow, const DataContext& context) {
    auto [daily_values, daily_cash_flows] = calculate_daily_values(flow, context);

    if (daily_values.empty() || daily_values.size() < 2) {
        return 0.0f;
    }

    // 计算初始投资（第一天的价值加上第一天的现金流）
    double initial_value = 0;
    for (auto val : daily_values) {
        if (val > 0) {
            initial_value = val;
            break;
        }
    }
    double initial_cash_flow = daily_cash_flows.front();

    // 如果初始投资为 0 或负数，返回 0
    if (initial_value + initial_cash_flow <= 0) {
        return 0.0f;
    }

    // 期末价值
    double final_value = daily_values.back();

    // 总现金流（不包括第一天）
    double total_cash_flow = 0.0;
    for (size_t i = 1; i < daily_cash_flows.size(); ++i) {
        total_cash_flow += daily_cash_flows[i];
    }

    // 总收益率 = (期末价值 - 期初价值 - 净现金流) / 期初价值
    // 或者简化为：(期末价值 + 总现金流入 - 总现金流出) / 期初投入 - 1
    double tatal_profit = final_value - initial_value - total_cash_flow;
    double total_return = tatal_profit / initial_value;

    return static_cast<float>(total_return);
}

float win_rate(const crash_flow_t& flow, const DataContext& context) {
    if (flow.empty()) {
        return 0.0f;
    }

    // 按 symbol 分组交易记录
    std::map<symbol_t, std::vector<TradeReport>> trades_by_symbol;
    for (const auto& [symbol, report] : flow) {
        trades_by_symbol[symbol].push_back(report);
    }

    int total_trades = 0;
    int winning_trades = 0;

    // 对每个 symbol，配对买入和卖出交易
    for (auto& [symbol, reports] : trades_by_symbol) {
        // 按时间排序
        std::sort(reports.begin(), reports.end(),
                  [](const TradeReport& a, const TradeReport& b) {
                      return a._time < b._time;
                  });

        // 配对交易：买入后卖出算一笔完整交易
        double total_buy_cost = 0.0;
        int total_buy_qty = 0;

        for (const auto& report : reports) {
            if (report._side == 0) {
                // 买入
                total_buy_cost += report._trade_amount;
                total_buy_qty += report._quantity;
            } else if (report._side == 1 && total_buy_qty > 0) {
                // 卖出
                double sell_amount = report._trade_amount;

                // 计算这笔卖出的盈亏
                // 假设 FIFO 原则
                int sell_qty = report._quantity;
                if (sell_qty <= total_buy_qty) {
                    // 完全盈利或亏损
                    double avg_buy_price = total_buy_cost / total_buy_qty;
                    double profit = (sell_amount / sell_qty - avg_buy_price) * sell_qty;

                    if (profit > 0) {
                        winning_trades++;
                    }
                    total_trades++;

                    total_buy_qty -= sell_qty;
                    total_buy_cost -= avg_buy_price * sell_qty;
                }
            }
        }
    }

    if (total_trades == 0) {
        return 0.0f;
    }

    return static_cast<float>(winning_trades) / total_trades;
}

float calmar_ratio(const crash_flow_t& flow, const DataContext& context, double freerate) {
    // 计算年化收益率
    auto [daily_values, daily_cash_flows] = calculate_daily_values(flow, context);

    if (daily_values.empty() || daily_values.size() < 2) {
        return 0.0f;
    }

    // 计算总收益率
    double initial_value = daily_values.front();
    double final_value = daily_values.back();
    double total_cash_flow = 0.0;

    for (size_t i = 1; i < daily_cash_flows.size(); ++i) {
        total_cash_flow += daily_cash_flows[i];
    }

    double total_return = (final_value - initial_value - total_cash_flow) / initial_value;

    // 计算年化收益率
    auto& times = context.GetTime();
    if (times.empty()) {
        return 0.0f;
    }

    time_t start_time = times.front();
    time_t end_time = times.back();
    double total_days = static_cast<double>(end_time - start_time) / (24 * 3600);

    if (total_days <= 0) {
        return 0.0f;
    }

    double annualized_return = 0.0;
    if (total_return > -1.0) {
        annualized_return = std::pow(1.0 + total_return, YEAR_DAY / total_days) - 1.0;
    }

    // 计算最大回撤
    float max_dd = max_drawdown_ratio(flow, context);

    // 卡玛比率 = 年化收益率 / 最大回撤绝对值
    if (max_dd <= 0) {
        // 如果没有回撤，返回一个较大的值或年化收益率本身
        return annualized_return > 0 ? 10.0f : 0.0f;
    }

    return static_cast<float>(annualized_return / max_dd);
}
