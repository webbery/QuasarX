#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

namespace portfolio {

/**
 * @brief 目标波动率 sizing
 *
 * 按 w = vol_target / σ_i 反推权重（σ 取自 context 的
 * `{symbol}.volatility`）。缺失时回退等权。
 *
 * 注意：`.volatility` 这个 key 目前**没有任何节点写入**（FunctionNode 用
 * ``STD`` 方法 + label="volatility" 才能产生），所以实际运行中这个模式
 * 基本总是走等权回退。
 */
class VolatilityTargetModel final : public PortfolioModel {
public:
    VolatilityTargetModel() { _method = SizingMethod::VolatilityTarget; }

    const char* name() const override { return "volatility_target"; }
    Map<symbol_t, double> compute(PortfolioContext& ctx) override;

protected:
    bool onInit(const nlohmann::json& config) override;

private:
    double _vol_target = 0.02;   // 日波动目标
};

}  // namespace portfolio
