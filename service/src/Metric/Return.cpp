#include "Metric/Return.h"
#include "Util/system.h"
#include "Bridge/exchange.h"
#include "std_header.h"

namespace {
    // 多空分离持仓模型
    struct Pos { int long_qty = 0; int short_qty = 0; };

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

Vector<double> simple_daily_return(const Vector<double>& daily_values, const Vector<double>& daily_cash_flows) {
    // 计算每日收益率: R_t = (V_t - V_{t-1} - CF_t) / V_{t-1}
    size_t cnt = (int)daily_values.size() - 1;
    Vector<double> rets(cnt);
    rets[0] = 0;  // 第一天收益率为 0
    for (size_t i = 1; i < cnt; ++i) {
        double prev = daily_values[i - 1];
        double curr = daily_values[i];
        double cf = daily_cash_flows[i];
        if (prev != 0.0) {
            rets[i] = (curr - prev - cf) / prev;
        } else {
            rets[i] = 0.0;
        }
    }
    return rets;
}

// 计算每日投资组合价值（现金 + 持仓市值）
// 返回: {daily_values, daily_cash_flows}
std::pair<std::vector<double>, std::vector<double>>
build_portfolio_values(const crash_flow_t& flow, const DataContext& context) {
    // 获取时间轴
    auto& times = context.GetTime();
    if (times.empty()) {
        return {{}, {}};
    }

    std::vector<double> daily_values(times.size(), 0.0);
    std::vector<double> daily_cash_flows(times.size(), 0.0);

    // 持仓记录（多空分离）
    std::map<symbol_t, Pos> positions;

    // 交易记录按时间分组
    std::map<time_t, std::vector<std::pair<symbol_t, TradeReport>>> trades_by_time;
    for (const auto& [symbol, report] : flow) {
        trades_by_time[report._time].push_back({symbol, report});
    }

    // 最新成交价
    std::map<symbol_t, double> last_prices;

    // 初始资金
    double initial_capital = context.getInitialCapital();
    double cash = initial_capital;

    // 遍历每个交易日
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

                last_prices[symbol] = report._price;
            }
        }

        // 组合价值 = 现金 + 持仓市值
        double portfolio_value = cash;
        for (const auto& [symbol, pos] : positions) {
            if (pos.long_qty == 0 && pos.short_qty == 0) continue;
            auto it = last_prices.find(symbol);
            if (it == last_prices.end()) continue;
            portfolio_value += pos.long_qty * it->second - pos.short_qty * it->second;
        }
        daily_values[i] = portfolio_value;
        // INFO("[BuildPortfolio] day={} time={} portfolio_value={:.2f} cash={:.2f} cash_flow={:.2f}",
        //      i, current_time, portfolio_value, cash, daily_cash_flows[i]);
    }

    return {daily_values, daily_cash_flows};
}
