#pragma once
#include "Nodes/Portfolio/PortfolioModel.h"

// 三个 MVO 重实现只在 MvoModel.cpp 里实例化（前向声明 + unique_ptr），
// 避免把 kalman(Eigen 模板) / OSQP 头文件拖进 PortfolioModel.cpp（工厂）
// 及其它模型。
namespace mvo {
class MeanEstimator;
class RiskModel;
}

namespace portfolio {

/**
 * @brief 均值方差优化（MVO）
 *
 * 路线（service/lab/portfolio/README.md 已验证）：
 *   1. 均值 μ：卡尔曼滤波估计时变均值（因子剥离 → 双精度滤波 → 总收益还原）
 *   2. 协方差 Σ：EWMA + 常数相关收缩 + PSD 兜底
 *   3. 优化：OSQP 解带换手惩罚的凸 QP
 *
 * 目标：
 * ```
 * max   μᵀw − (γ/2)·wᵀΣw − κ·c·‖w − w_prev‖₁
 * s.t.  Σ w_i = e,  0 ≤ w_i ≤ w_max
 * ```
 *
 * ── 阶段一（当前实现）：买入侧 ──
 * 候选集 = 有 BUY 信号**且当前无持仓**的标的。已持仓标的不参与求解 ——
 * generatePlan 对"已持仓的 BUY"会直接跳过（不加仓），把权重分给它们等于
 * 浪费预算。因此本阶段的换手惩罚作用于"MVO 权重的变化速度"，而不是真实
 * 持仓换手。
 *
 * TODO(阶段二): 候选集扩到「BUY ∪ 已持仓未清仓」，w_prev 换成真实持仓漂移
 *   权重，并给 generatePlan 加"部分减仓"能力（现在 SELL 分支写死全清）。
 *   理由见 README 结论 #3：换手主要来自信号驱动的进出场，仅靠本层的软惩罚
 *   压不住；要降换手需要换手预算硬约束 + 只动变化的名字。
 */
class MvoModel final : public PortfolioModel {
public:
    MvoModel();
    ~MvoModel() override;

    void Prepare() override;
    Map<symbol_t, double> compute(PortfolioContext& ctx) override;
    const char* name() const override { return "mvo"; }

protected:
    bool onInit(const nlohmann::json& config) override;

private:
    // ── 分支算法参数 ──
    String _mean_method = "kalman";        // kalman | sample
    String _cov_method = "ewma_shrunk";    // ewma_shrunk | sample | ledoit_wolf

    // ── 优化器参数 ──
    double _risk_aversion = 6.0;           // γ
    double _turnover_penalty = 1.0;        // κ
    double _turnover_horizon = 10.0;       // H：预期持有天数
    double _cost_rate = 8.4e-4;            // 单边综合成本率
    double _max_weight = 0.60;             // 单标的上限
    double _vol_target = 0.20;             // 波动率目标（年化）
    bool   _vol_target_enabled = false;    // README 结论 #6：当前是负贡献
    int    _min_samples = 60;              // 收益样本不足 → 回退等权
    int    _min_names = 2;                 // 候选数不足 → 回退等权

    // ── 运行时状态（跨 bar 保持，Prepare 重置）──
    // 裸指针 + 前向声明：把 kalman/OSQP 头文件挡在 MvoModel.cpp 里，
    // 工厂（PortfolioModel.cpp）和 PortfolioNode.h 都不必解析它们。
    mvo::MeanEstimator* _estimator = nullptr;
    mvo::RiskModel*     _risk = nullptr;
    Map<symbol_t, double> _prev_weights;   // 上一期 MVO 权重（换手惩罚的 w_prev）
};

}  // namespace portfolio
