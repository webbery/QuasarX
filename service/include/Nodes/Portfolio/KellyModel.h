#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

namespace portfolio {

/**
 * @brief Kelly 公式 sizing
 *
 * 简化版：用历史胜率估算 f* = (b·p − q) / b，其中 b = 平均盈利/平均亏损。
 * 从 context 的 `{symbol}.win_rate` 读取胜率；缺失时回退等权。
 *
 * TODO: 平均盈利 / 平均亏损目前是硬编码的 2%，应改为从成交记录或
 *       `{symbol}.avg_win` / `{symbol}.avg_loss` 读取。
 */
class KellyModel final : public PortfolioModel {
public:
    KellyModel() { _method = SizingMethod::Kelly; }

    const char* name() const override { return "kelly"; }
    Map<symbol_t, double> compute(PortfolioContext& ctx) override;
};

}  // namespace portfolio
