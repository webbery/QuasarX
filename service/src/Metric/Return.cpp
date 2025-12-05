#include "Metric/Return.h"
#include "Util/system.h"
#include "DataGroup.h"

namespace {
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
        return annual_returns.back();
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
            auto& closes = std::get<std::vector<double>>(context.get(key));
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
                if (report._type == 'B' || report._type == 'b') {  // 买入
                    positions[symbol] += report._quantity;
                    daily_cash_flows[i] -= report._trade_amount;  // 现金流出
                } else if (report._type == 'S' || report._type == 's') {  // 卖出
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
        
        // 加上现金部分（如果有现金账户数据）
        // 这里可以扩展：从context获取现金余额
        daily_values[i] = portfolio_value;
    }
    
    // 2. 计算每日收益率序列
    std::vector<double> daily_returns;
    for (i = 1; i < times.size(); ++i) {
        double prev_value = daily_values[i-1];
        double curr_value = daily_values[i];
        double cash_flow = daily_cash_flows[i];
        
        if (prev_value != 0.0) {
            double daily_return = (curr_value - prev_value - cash_flow) / prev_value;
            daily_returns.push_back(daily_return);
        } else {
            daily_returns.push_back(0.0);
        }
    }
    
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
