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
 * ── 阶段一（当前实现）：多头腿求解 + 组尺度折算 ──
 * QP 仍然是 long-only，候选 = 本轮开仓的**多头腿**（腿表里只有新开仓，
 * 不再需要按持仓过滤）。求解后按组折算：
 *
 *     scale[g] = 组内已定价腿的 QP 权重之和
 *
 * 独立腿精确等于 QP 权重；配对组只有多头腿进 QP → 空头腿由 distribute
 * 按 share（= β）生成，两腿名义合计 = QP 权重，不会超预算。
 * 纯空头组（组内没有多头腿）拿剩余预算等分，避免做空腿完全没有权重。
 *
 * TODO(阶段二): QP 支持带符号权重（Σ|w| = e），或把配对组当合成资产
 *   （价差收益）进 QP。理由：`Σw = e` 只约束多头名义，配对的风险口径
 *   仍是"单标的名义"而不是"价差敞口"。
 */
class MvoModel final : public PortfolioModel {
public:
    MvoModel();
    ~MvoModel() override;

    void Prepare() override;
    const char* name() const override { return "mvo"; }

protected:
    bool onInit(const nlohmann::json& config) override;
    void onUniverse(const Set<symbol_t>& universe) override;
    Vector<double> computeScales(PortfolioContext& ctx) override;
    bool capsOnOutput() const override { return false; }

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

    // ── 模型专属输入 ──
    // 因子剥离要用全池等权收益做市场代理，所以需要节点交易池。
    // 它在 Init 时一次性给出（onUniverse），不进共享的 PortfolioContext。
    Set<symbol_t> _universe;

    // ── 运行时状态（跨 bar 保持，Prepare 重置）──
    // 裸指针 + 前向声明：把 kalman/OSQP 头文件挡在 MvoModel.cpp 里，
    // 工厂（PortfolioModel.cpp）和 PortfolioNode.h 都不必解析它们。
    mvo::MeanEstimator* _estimator = nullptr;
    mvo::RiskModel*     _risk = nullptr;
    Map<symbol_t, double> _prev_weights;   // 上一期 MVO 权重（换手惩罚的 w_prev）
    bool _warned_short_only = false;       // "纯空头组"告警限频
};

}  // namespace portfolio
