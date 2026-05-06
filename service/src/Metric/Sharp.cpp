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

// 计算年化波动率
// 公式: annualized_vol = std(daily_returns) * sqrt(YEAR_DAY)
// 参数: daily_returns - 每日收益率序列
// 返回: 年化波动率（标准差）
float compute_annualized_volatility(const Vector<double>& daily_returns) {
    if (daily_returns.size() < 2) {
        return 0.0f;
    }

    // 计算均值
    double sum = 0.0;
    for (double ret : daily_returns) {
        sum += ret;
    }
    double mean = sum / daily_returns.size();

    // 计算样本标准差（Bessel 修正）
    double sum_sq = 0.0;
    for (double ret : daily_returns) {
        double diff = ret - mean;
        sum_sq += diff * diff;
    }
    double std_daily = std::sqrt(sum_sq / (daily_returns.size() - 1));

    // 年化
    return static_cast<float>(std_daily * std::sqrt(static_cast<double>(YEAR_DAY)));
}

// 计算夏普比率（简化版）
// 公式: sharp = (annualized_return - risk_free_rate) / annualized_volatility
// 参数: annualized_return - 年化收益率
//       annualized_volatility - 年化波动率
//       risk_free_rate - 无风险利率（年化）
// 返回: 夏普比率
float compute_sharp_ratio(float annualized_return, float annualized_volatility, double risk_free_rate) {
    if (annualized_volatility <= 0) {
        return 0.0f;
    }
    return static_cast<float>((annualized_return - risk_free_rate) / annualized_volatility);
}