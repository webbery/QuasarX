#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

namespace portfolio {

/**
 * @brief 信号强度加权 sizing
 *
 * 读取 context 的 `{symbol}.strength`，正值归一化成权重。
 * 缺失 / 非正时给一个最小权重（1e-6），避免完全排除。
 *
 * 权重域是 PortfolioContext::_open_symbols —— 本轮**要建仓**的腿
 * （BUY 新开多 + SELL 新开空）。覆盖空头腿是配对交易能按 β 配平的前提：
 * 配对的两条腿方向相反，只算 BUY 腿的话空头腿拿不到 {B}.strength = β，
 * 只能退回 PortfolioNode 的兜底公式 → 两腿名义比恒为 1:2 而不是 1:β。
 * 两腿一起归一化后 weight_A = 1/(1+β)、weight_B = β/(1+β)。
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
