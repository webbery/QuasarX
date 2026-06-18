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