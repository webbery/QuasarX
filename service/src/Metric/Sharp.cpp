#include "Metric/Sharp.h"
#include "Util/system.h"
#include "Bridge/exchange.h"
#include <algorithm>

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