#pragma once
#include "std_header.h"
#include "Algorithms/MVO/MvoTypes.h"
#include <Eigen/Dense>
#include <vector>

namespace mvo {

/**
 * @brief 均值-方差优化器参数
 *
 * 口径对齐 service/lab/portfolio/pf/optimizer.py（README §2.7）。
 */
struct MvoParams {
    OptimizerMethod _method = OptimizerMethod::Ospq;

    double _risk_aversion = 6.0;       // γ：风险厌恶（μ/Σ 均为年化口径）
    double _turnover_penalty = 1.0;    // κ：换手惩罚系数
    double _cost_rate = 0.0014;        // c：单边综合成本率（**已折算**年化）
    double _max_weight = 0.60;         // 单标的上限
    double _max_total_exposure = 1.0;  // 目标敞口 e
    double _vol_target = 0.20;         // 波动率目标（年化）
    bool   _vol_target_enabled = true;
    double _min_expected_excess = 0.0; // 组合层面的现金门槛（0=不设）

    int    _max_iter = 5;              // 波动率目标的不动点迭代轮数

    // ── OSQP 求解器 ─────────────────────────────────────
    double _eps_abs = 1e-6;
    double _eps_rel = 1e-6;
    int    _max_iter_qp = 4000;
};

/**
 * @brief 求解结果
 */
struct MvoResult {
    bool               _ok = false;
    std::vector<double> _weights;
    String             _note;      // 诊断信息（失败原因 / 求解器状态）
};

/**
 * @brief 凸 QP 均值-方差优化器（OSQP）
 *
 * 求解：
 * ```
 * max   μᵀw − (γ/2)·wᵀΣw − κ·c·‖w − w_prev‖₁
 * s.t.  Σ w_i = e              目标敞口
 *       0 ≤ w_i ≤ w_max        单标的上限
 * ```
 *
 * ‖w − w_prev‖₁ 不可微，用辅助变量 u_i ≥ |w_i − w_prev_i| 线性化后整体
 * 是纯 QP，OSQP 可直接解。
 *
 * 变量 x = [w(N); u(N)]，2N 维；约束 m = 4N+1 行。
 *
 * **量纲约定**：μ、Σ 都必须是年化口径（μ 年化收益、Σ 年化方差），这样
 * 目标函数里 μ'w 与 w'Σw 同量纲，γ 才有稳定含义。把两者同时除以 244、
 * γ 乘 244 数学上等价，但协方差缩到 1e-5 量级后相对求解器容差太小，
 * 实测解会偏离解析解 —— 所以一律用年化口径。
 */
class MvoOptimizer {
public:
    explicit MvoOptimizer(const MvoParams& params = MvoParams());

    /// 求权重（μ、Σ 年化口径；w_prev 为上一期权重）
    MvoResult solve(const Eigen::VectorXd& mu,
                    const Eigen::MatrixXd& cov,
                    const Eigen::VectorXd& w_prev);

    const MvoParams& params() const { return _params; }

private:
    /// 单次 QP 求解（给定敞口 e）
    bool solveQp(const Eigen::VectorXd& mu,
                 const Eigen::MatrixXd& cov,
                 const Eigen::VectorXd& w_prev,
                 double exposure,
                 Eigen::VectorXd& w_out,
                 String& note);

    MvoParams _params;
};

}  // namespace mvo
