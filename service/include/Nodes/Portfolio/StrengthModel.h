#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

namespace portfolio {

/**
 * @brief 信号强度加权 sizing
 *
 * 读取 context 的 `{symbol}.strength`，正值归一化成权重。
 * 缺失 / 非正时给一个最小权重（1e-6），避免完全排除。
 *
 * 注意：`.strength` 目前只有 PairNode 会写（Sigma 的 XGBoost 打分走的是
 * debug 导出的 `.raw_strength`，不进 context），所以纯 SignalNode 链路下
 * 这个模式实际退化等权。
 */
class StrengthModel final : public PortfolioModel {
public:
    StrengthModel() { _method = SizingMethod::Strength; }

    const char* name() const override { return "strength"; }
    Map<symbol_t, double> compute(PortfolioContext& ctx) override;
};

}  // namespace portfolio
