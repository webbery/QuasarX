#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

namespace portfolio {

/**
 * @brief Kelly 公式 sizing（组级尺度 + strength 腿间拆分）
 *
 * f* = (b·p − q)/b，其中 p = 胜率、b = 平均盈利/平均亏损、q = 1−p。
 *
 * 语义分工（这是能支持做空组合的关键）：
 *   - 凯利输出的是**组内总尺度** f：这个配对（或这个标的）该占多少预算；
 *   - 组内两腿怎么分，由 strength（= 1 : β）决定 —— 那是 β 对冲系数的职责。
 * 把两种语义混在一张 symbol → weight 表里，配对就算不对：要么空头腿拿不到
 * 权重（退化成 1:2），要么凯利把腿间比例也算歪。
 *
 * 统计输入是**组级**的（{key}.win_rate / .avg_win / .avg_loss / .samples）：
 * 配对的两条腿方向相反，各自胜率天然 ≈ 50%，腿级统计没有任何信息量。
 * 单腿组沿用 "{symbol}.win_rate"（与改造前键名一致）。
 */
class KellyModel final : public PortfolioModel {
public:
    KellyModel() { _method = SizingMethod::Kelly; }

    const char* name() const override { return "kelly"; }
    Map<symbol_t, double> compute(PortfolioContext& ctx) override;

protected:
    bool onInit(const nlohmann::json& config) override;
    void Prepare() override;

private:
    /// 已平仓笔数下限 —— 样本太少时胜率/赔率是噪声，凯利会把仓位算得很极端
    int _min_samples = 10;
    /// 已经告警过的组（样本不足时每根 bar 都会命中 → 限频）
    Set<uint64_t> _warned_groups;
};

}  // namespace portfolio
