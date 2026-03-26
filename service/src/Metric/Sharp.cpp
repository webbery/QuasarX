#include "std_header.h"
#include "Metric/Sharp.h"
#include "Util/system.h"
#include "Bridge/exchange.h"
#include <algorithm>

namespace {
    // 辅助结构：子区间数据
    struct SubPeriod {
        time_t start_time;  // 开始时间
        time_t end_time;    // 结束时间
        double start_value; // 开始市值
        double end_value;   // 结束市值
        std::map<symbol_t, int> positions; // 持仓数量
    };

    // 计算没有现金流场景下的收益率
    float non_flow_return_rate(const SubPeriod& period,
                                const std::map<symbol_t, Vector<double>>& price_data,
                                const List<time_t>& times) {
        // 如果没有持仓，返回 0
        if (period.positions.empty()) {
            return 0.0;
        }

        double start_total = period.start_value;
        if (start_total <= 0) {
            return 0.0;
        }

        double end_total = 0.0;

        // 计算结束时的总市值
        for (auto& [symbol, qty] : period.positions) {
            if (qty == 0) continue;

            auto it = price_data.find(symbol);
            if (it == price_data.end() || it->second.empty()) continue;

            // 找到结束时间对应的价格
            auto range = std::equal_range(times.begin(), times.end(), period.end_time);
            if (range.first == range.second)
                continue;

            auto end_price = it->second.at(std::distance(times.begin(), range.first));
            end_total += qty * end_price;
        }

        return (end_total - start_total) / start_total;
    }

    // 获取时间对应的索引
    int find_time_index(const List<time_t>& times, time_t target_time) {
        auto itr = times.begin();
        for (size_t i = 0; i < times.size(); ++i, ++itr) {
            if (*itr >= target_time) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
}

float sharp_ratio(const crash_flow_t& flow, const DataContext& context, double freerate) {
    const double ANNUAL_RISK_FREE_RATE = freerate;
    const double DAILY_RISK_FREE_RATE = ANNUAL_RISK_FREE_RATE / YEAR_DAY;

    Set<symbol_t> symbols;
    // 现金流成交记录
    Map<symbol_t, List<TradeReport>> reports;
    // 获取时间轴
    auto& times = context.GetTime();
    if (times.empty()) {
        return 0.0f;
    }
     // 1. 组织交易记录
    for (auto& item: flow) {
        symbols.insert(item.first);
        reports[item.first].emplace_back(item.second);
    }
    // 2. 准备价格数据
    std::map<symbol_t, Vector<double>> price_data;
    for (auto& symbol : symbols) {
        String name = get_symbol(symbol);
        String key = name + ".close";
        try {
            auto& closes = context.get<Vector<double>>(key);
            price_data[symbol] = closes;
        } catch (...) {
            // 如果获取数据失败，跳过这个 symbol
            continue;
        }
    }

    // 3. 识别所有现金流时间点（交易发生的时间）
    List<time_t> trade_times;
    std::set<time_t> unique_times;
    for (const auto& [symbol, report_list] : reports) {
        for (const auto& report : report_list) {
            if (unique_times.find(report._time) == unique_times.end()) {
                unique_times.insert(report._time);
                trade_times.push_back(report._time);
            }
        }
    }
    trade_times.push_back(times.front());
    trade_times.push_back(times.back());
    // 排序时间点
    trade_times.sort();
    trade_times.unique();  // 去重

    std::vector<double> sub_period_returns;
    std::map<symbol_t, int> current_positions;  // 当前持仓

    // 构建时间到持仓的映射：用于计算每日波动率
    std::map<time_t, std::map<symbol_t, int>> time_positions;
    time_positions[times.front()] = current_positions;  // 初始空仓
    // 4. 分割区间并计算每个子区间的收益率
    for (auto itr = trade_times.begin(); itr != trade_times.end(); ) {
        time_t period_start = *itr;
        ++itr;
        if (itr == trade_times.end()) {
            break;
        }
        time_t period_end = *itr;

        // 创建子区间
        SubPeriod period;
        period.start_time = period_start;
        period.end_time = period_end;
        // 获取区间开始时的持仓（来自上一个区间结束）
        period.positions = current_positions;
        // 计算区间开始时的市值
        period.start_value = 0.0;
        for (const auto& [symbol, qty] : period.positions) {
            auto it = price_data.find(symbol);
            if (it == price_data.end()) continue;

            // 找到开始时间对应的价格
            int start_idx = find_time_index(times, period_start);
            if (start_idx < 0 || start_idx >= static_cast<int>(it->second.size())) {
                continue;
            }

            double start_price = it->second[start_idx];
            period.start_value += qty * start_price;
        }

        // 处理这个区间内的所有交易，更新持仓
        for (const auto& [symbol, report_list] : reports) {
            for (const auto& report : report_list) {
                if (report._time >= period_start && report._time < period_end) {
                    // 更新持仓
                    if (report._side == 0) {  // 买入
                        current_positions[symbol] += report._quantity;
                    } else if (report._side == 1) {  // 卖出
                        current_positions[symbol] -= report._quantity;
                    }
                }
            }
        }
        // 记录时间点上的持仓（用于波动率计算）
        time_positions[period_end] = current_positions;
        // 计算子区间收益率
        auto period_return = non_flow_return_rate(period, price_data, times);
        sub_period_returns.push_back(period_return);
    }
    // 5. 如果没有子区间收益率，返回 0
    if (sub_period_returns.empty()) {
        return 0.0f;
    }
    // 6. 连接所有子区间收益率，计算累计收益率
    // 使用时间加权收益率：总收益率 = ∏(1 + R_i) - 1
    double total_return = 1.0;
    for (double ret : sub_period_returns) {
        total_return *= (1.0 + ret);
    }
    total_return -= 1.0;

    // 7. 计算年化收益率
    // 首先计算总交易天数
    double total_days = 0.0;
    if (!times.empty()) {
        time_t start = times.front();
        time_t end = times.back();
        total_days = static_cast<double>(end - start) / (24 * 3600);  // 转换为天数
    }
    if (total_days <= 0) {
        return 0.0f;
    }
    double annualized_return = 0.0;
    if (total_return > -1.0) {  // 确保收益率大于 -100%
        annualized_return = pow(1.0 + total_return, YEAR_DAY / total_days) - 1.0;
    } else {
        INFO("total return {}", total_return);
    }

    // 8. 计算日收益率序列（用于计算波动率）
    // 考虑持仓变化的日收益率计算
    std::vector<double> daily_returns;

    // 按时间顺序遍历，计算每日组合价值和收益率
    std::map<symbol_t, int> running_positions;  // 逐日持仓
    std::map<time_t, std::vector<std::pair<symbol_t, TradeReport>>> trades_by_time;
    for (const auto& [symbol, report_list] : reports) {
        for (const auto& report : report_list) {
            trades_by_time[report._time].push_back({symbol, report});
        }
    }

    double prev_portfolio_value = 0.0;
    bool has_prev_value = false;

    size_t day_idx = 0;
    for (auto itr = times.begin(); itr != times.end(); ++day_idx, ++itr) {
        time_t current_time = *itr;

        // 处理当前时间点的交易，更新持仓
        if (trades_by_time.find(current_time) != trades_by_time.end()) {
            for (const auto& [symbol, report] : trades_by_time[current_time]) {
                if (report._side == 0) {  // 买入
                    running_positions[symbol] += report._quantity;
                } else if (report._side == 1) {  // 卖出
                    running_positions[symbol] -= report._quantity;
                }
            }
        }

        // 计算当前组合价值
        double current_portfolio_value = 0.0;
        for (const auto& [symbol, qty] : running_positions) {
            if (qty == 0) continue;
            auto it = price_data.find(symbol);
            if (it == price_data.end()) continue;
            const auto& closes = it->second;
            if (day_idx < closes.size()) {
                current_portfolio_value += qty * closes[day_idx];
            }
        }

        // 计算日收益率
        if (has_prev_value && prev_portfolio_value > 0) {
            double daily_return = (current_portfolio_value - prev_portfolio_value) / prev_portfolio_value;
            daily_returns.push_back(daily_return);
        }

        prev_portfolio_value = current_portfolio_value;
        has_prev_value = true;
    }

    // 9. 计算年化波动率
    if (daily_returns.size() < 2) {
        return 0.0f;
    }

    // 计算日收益率的平均值
    double sum_returns = 0.0;
    for (double ret : daily_returns) {
        sum_returns += ret;
    }
    double mean_daily_return = sum_returns / daily_returns.size();

    // 计算标准差
    double sum_squared_diffs = 0.0;
    for (double ret : daily_returns) {
        double diff = ret - mean_daily_return;
        sum_squared_diffs += diff * diff;
    }
    double std_daily_return = std::sqrt(sum_squared_diffs / (daily_returns.size() - 1));
    // 年化波动率
    double annualized_volatility = std_daily_return * std::sqrt(static_cast<double>(YEAR_DAY));

    // 10. 计算夏普比率
    if (annualized_volatility <= 0) {
        return 0.0f;
    }

    return (annualized_return - ANNUAL_RISK_FREE_RATE) / annualized_volatility;
}
