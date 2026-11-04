#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

namespace portfolio {

/**
 * @brief 等权分配
 *
 * 返回空 map —— 调用方（PortfolioNode::generatePlan）在 _weights 为空时
 * 按 targetCapital / 标的数 均分。这是引擎的历史行为，作为基线保留。
 */
class EqualModel final : public PortfolioModel {
public:
    EqualModel() { _method = SizingMethod::Equal; }

    const char* name() const override { return "equal"; }
    Map<symbol_t, double> compute(PortfolioContext& ctx) override;
};

}  // namespace portfolio
