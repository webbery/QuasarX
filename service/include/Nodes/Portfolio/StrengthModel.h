#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

namespace portfolio {

/**
 * @brief 信号强度加权 sizing
 *
 * 定仓单元之间等权（每组 1/G），组内按 share 拆。`share` 由节点从
 * `{symbol}.strength` 归一化得到 —— PairNode 发布 1 : β 时，配对的两腿
 * 名义比就是 1 : β（与 backtest_pair.py 的 not_a = E/(1+β)、
 * not_b = E·β/(1+β) 同口径）。
 *
 * 独立多空（无组）时 share = 1，本模型退化为等权：跨截面的"按信号强度
 * 加权"需要组级置信度输入（`group.<id>.strength`），当前还没有生产者。
 */
class StrengthModel final : public PortfolioModel {
public:
    StrengthModel() { _method = SizingMethod::Strength; }

    const char* name() const override { return "strength"; }

protected:
    Vector<double> computeScales(PortfolioContext& ctx) override;
};

}  // namespace portfolio
