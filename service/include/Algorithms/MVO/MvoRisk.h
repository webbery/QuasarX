#pragma once
#include "std_header.h"
#include "Algorithms/MVO/MvoTypes.h"
#include <Eigen/Dense>

namespace mvo {

/**
 * @brief 风险（协方差）模型配置
 *
 * 口径对齐 service/lab/portfolio/pf/risk.py（README §2.6）。
 *
 * 注意：README 的消融组"无协方差收缩"不需要单独的方法 —— 把
 * `_shrinkage` 设为 0 即可（实测夏普 1.60 → 1.63，基本中性）。
 */
struct RiskModelConfig {
    CovMethod _method = CovMethod::EwmaShrunk;
    double _cov_halflife = 60.0;     // EWMA 半衰期（交易日）
    double _shrinkage = 0.30;        // 向常数相关目标收缩的强度 δ
    double _psd_floor = 1e-12;       // PSD 兜底的特征值下限
    int    _trading_days = YEAR_DAY; // 年化因子
};

/**
 * @brief 向"所有相关系数都等于平均相关 ρ̄"的目标收缩
 *
 * Σ̂ = (1−δ)·Σ + δ·diag(Σ)·C̄·diag(Σ)，C̄ = (1−ρ̄)I + ρ̄·11ᵀ。
 *
 * ⚠ ρ̄ 的口径：这里取的是**平均成对相关**（只对 i<j 求和再除以对数）。
 * lab 的 pf/risk.py 把全部非对角元素（含 (i,j) 与 (j,i)）求和后除以
 * n(n−1)/2，ρ̄ 被放大了 2 倍 —— 实测 5 资产样本上真实平均相关 0.3624，
 * lab 算出 0.7248（已超过任意一对资产的实际相关 0.54）。本实现按 README
 * 自己写下的口径（"所有相关系数都等于平均相关 ρ̄"）取正确值；复现 lab
 * 数值需要把 ρ̄ 乘 2。
 *
 * @param cov   待收缩的协方差矩阵（需为正定/半正定）
 * @param delta 收缩强度（0 = 不收缩）
 * @return 收缩后的对称矩阵
 */
Eigen::MatrixXd shrinkConstantCorrelation(const Eigen::MatrixXd& cov, double delta);

/**
 * @brief 特征值截断，保证半正定
 *
 * 收缩后仍可能因数值误差出现极小的负特征值；不修的话 QP 求解器会返回
 * 不可行解。
 */
Eigen::MatrixXd nearestPsd(const Eigen::MatrixXd& cov, double floor = 1e-12);

/**
 * @brief 逐日 EWMA 协方差风险模型
 *
 * 默认路径三层处理（README §2.6）：
 *   1. EWMA：Σ_t = (1−λ)Σ_{t−1} + λ(x_t − μ̄_{t−1})(x_t − μ̄_{t−1})ᵀ
 *   2. 收缩到常数相关目标（δ）
 *   3. PSD 兜底（特征值截断）
 *
 * 对外返回**年化**协方差（× YEAR_DAY），与年化的 μ 保持同一量纲 ——
 * 目标函数里 μ'w 与 w'Σw 必须都是"年化收益/年化方差"，风险厌恶系数 γ
 * 才有稳定含义。
 */
class RiskModel {
public:
    explicit RiskModel(const RiskModelConfig& cfg = RiskModelConfig());

    /// 重置递推状态
    void reset();

    /// 换配置（会重置递推状态）
    void setConfig(const RiskModelConfig& cfg);

    /// 推进（T×n 收益面板，行=时间，列=资产，NaN=缺失），返回当前年化协方差 (n×n)
    Eigen::MatrixXd update(const Eigen::MatrixXd& returns_history);

    /// 当前年化协方差 (n×n)
    const Eigen::MatrixXd& covariance() const { return _covAnnualized; }

    const RiskModelConfig& config() const { return _cfg; }

private:
    /// 缺失值按 0 处理的收益矩阵（与 EWMA 路径同口径：当日该资产无变动）
    static Eigen::MatrixXd sanitize(const Eigen::MatrixXd& returns_history);

    /// 样本协方差（T−1 分母）
    static Eigen::MatrixXd sampleCovariance(const Eigen::MatrixXd& x);

    RiskModelConfig  _cfg;
    Eigen::VectorXd  _mean;           // EWMA 一阶矩
    Eigen::MatrixXd  _cov;            // EWMA 协方差（日频）
    Eigen::MatrixXd  _covAnnualized;  // 对外口径（年化）
    int              _consumed = 0;   // 已推进的行数
    int              _n = 0;
    bool             _firstFrame = true;
};

}  // namespace mvo
