#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

namespace portfolio {

/**
 * @brief 目标波动率 sizing
 *
 * 按 scale = vol_target / σ_group 反推每个定仓单元的尺度（σ 取自 context 的
 * `{key}.volatility`，key 见 `groupKey()`）。没有组级 σ 时用腿级 σ 按
 * share 合成：σ_group = |Σ dir_i · share_i · σ_i| / Σ share_i —— 配对的两条
 * 腿方向相反，多空互换不改变这个值（整体取负）。缺失时回退等权。
 *
 * 注意：`.volatility` 这个 key 目前**没有任何节点写入**（FunctionNode 用
 * ``STD`` 方法 + label="volatility" 才能产生），所以实际运行中这个模式
 * 基本总是走等权回退。
 */
class VolatilityTargetModel final : public PortfolioModel {
public:
    VolatilityTargetModel() { _method = SizingMethod::VolatilityTarget; }

    const char* name() const override { return "volatility_target"; }

protected:
    bool onInit(const nlohmann::json& config) override;
    Vector<double> computeScales(PortfolioContext& ctx) override;

private:
    double _vol_target = 0.02;   // 日波动目标
};

}  // namespace portfolio
