#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

namespace portfolio {

/**
 * @brief 等权分配
 *
 * 返回空尺度 —— 基类按 1/G 兜底，G = 定仓单元数：
 *   - 独立多空：每条腿一个单元 → 每条腿均分预算；
 *   - 配对 / 篮子：每个组一个单元 → 组间均分，组内再按 share 拆
 *     （配对即 1:β，两腿名义合计回到 100% 预算）。
 */
class EqualModel final : public PortfolioModel {
public:
    EqualModel() { _method = SizingMethod::Equal; }

    const char* name() const override { return "equal"; }

protected:
    Vector<double> computeScales(PortfolioContext& ctx) override;
};

}  // namespace portfolio
