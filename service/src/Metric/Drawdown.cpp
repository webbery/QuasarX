#include "Metric/Drawdown.h"
#include "Util/system.h"
#include "Bridge/exchange.h"
#include <algorithm>
#include <cmath>

namespace {
    // 多空分离持仓模型
    struct Position {
        int long_qty = 0;   // 多头持仓
        int short_qty = 0;  // 空头持仓

        bool empty() const { return long_qty == 0 && short_qty == 0; }
    };

    // 根据交易报告更新持仓（多空分离模型）
    void update_position(std::map<symbol_t, Position>& positions,
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

        // 持仓记录：symbol -> 持仓（多空分离）
        std::map<symbol_t, Position> positions;

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
        daily_cash_flows[0] = context.getInitialCapital();
        for (auto itr = times.begin(); itr != times.end(); ++i, ++itr) {
            time_t current_time = *itr;

            if (i > 0) {
                daily_cash_flows[i] = daily_cash_flows[i - 1];
            }
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
        double value = daily_values[i] + daily_cash_flows[i];

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
    double initial_value = daily_values.front();
    double initial_cash_flow = daily_cash_flows.front();
    double initial_capital = initial_value + initial_cash_flow;
    // 如果初始投资为 0 或负数，返回 0
    if (initial_capital <= 0) {
        return 0.0f;
    }

    // 期末价值
    double final_value = daily_values.back();

    // 期间净现金流 = 最后一天累计现金流 - 初始本金
    // daily_cash_flows 是累计值，包含了 initial_capital 和所有交易现金流
    double total_cash_flow = daily_cash_flows.back() - daily_cash_flows.front();

    // 总收益率 = (期末价值 - 期初价值 - 净现金流) / 期初价值
    // 或者简化为：(期末价值 + 总现金流入 - 总现金流出) / 期初投入 - 1
    double total_profit = final_value - initial_capital - total_cash_flow;
    double total_return = total_profit / initial_capital;

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
        // 注意：做空场景下，先卖出开空再买入平空也算一笔完整交易
        double total_buy_cost = 0.0;
        int total_buy_qty = 0;
        double total_sell_value = 0.0;
        int total_sell_qty = 0;

        for (const auto& report : reports) {
            if (report._side == 0) {  // 买入
                if (report._flag == 1 && total_sell_qty > 0) {
                    // 买入平空：与之前的空仓配对
                    int match_qty = std::min(report._quantity, total_sell_qty);
                    double avg_sell_price = total_sell_value / total_sell_qty;
                    double profit = (avg_sell_price - report._price) * match_qty;

                    if (profit > 0) {
                        winning_trades++;
                    }
                    total_trades++;

                    total_sell_qty -= match_qty;
                    total_sell_value -= avg_sell_price * match_qty;
                } else if (report._flag == 0) {
                    // 买入开多：记录成本
                    total_buy_cost += report._trade_amount;
                    total_buy_qty += report._quantity;
                }
            } else {  // 卖出
                if (report._flag == 1 && total_buy_qty > 0) {
                    // 卖出平多：与之前的多仓配对
                    int match_qty = std::min(report._quantity, total_buy_qty);
                    double avg_buy_price = total_buy_cost / total_buy_qty;
                    double profit = (report._price - avg_buy_price) * match_qty;

                    if (profit > 0) {
                        winning_trades++;
                    }
                    total_trades++;

                    total_buy_qty -= match_qty;
                    total_buy_cost -= avg_buy_price * match_qty;
                } else if (report._flag == 0) {
                    // 卖出开空：记录卖空价值
                    total_sell_value += report._trade_amount;
                    total_sell_qty += report._quantity;
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
    double total_return = total_return_ratio(flow, context);

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
