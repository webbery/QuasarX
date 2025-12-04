#include "Metric/Return.h"
#include "std_header.h"

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
        double years = static_cast<double>(count) / 252.0;  // 假设252个交易日
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

float annual_return_ratio(const crash_flow_t& flow, const DataContext& context, int mode) {

}
