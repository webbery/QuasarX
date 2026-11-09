#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

namespace portfolio {

/**
 * @brief Kelly 公式 sizing（组级尺度 + 组内 share 拆分）
 *
 * f* = (b·p − q)/b，其中 p = 胜率、b = 平均盈利/平均亏损、q = 1−p。
 *
 * 语义分工（这是能支持做空组合的关键）：
 *   - 凯利输出的是**组内总尺度** f：这个配对（或这个标的）该占多少预算；
 *   - 组内两腿怎么分，由 share（配对 = 1 : β）决定 —— 那是 β 对冲系数的职责。
 *
 * 统计输入是**组级**的（`{key}.win_rate / .avg_win / .avg_loss / .samples`，
 * key 见 `groupKey()`）：配对的两条腿方向相反，各自胜率天然 ≈ 50%，腿级
 * 统计没有任何信息量。独立腿的 key 是标的代码（与改造前口径一致）。
 */
class KellyModel final : public PortfolioModel {
public:
    KellyModel() { _method = SizingMethod::Kelly; }

    const char* name() const override { return "kelly"; }

protected:
    bool onInit(const nlohmann::json& config) override;
    void Prepare() override;
    Vector<double> computeScales(PortfolioContext& ctx) override;

private:
    /// 已平仓笔数下限 —— 样本太少时胜率/赔率是噪声，凯利会把仓位算得很极端
    int _min_samples = 10;
    /// 已经告警过的组（样本不足时每根 bar 都会命中 → 限频）
    Set<String> _warned_groups;
};

}  // namespace portfolio
