#pragma once
#include "std_header.h"
#include "Algorithms/MVO/MvoTypes.h"
#include <Eigen/Dense>
#include <vector>

// 卡尔曼滤波库（third_party/include/kalman，Markus Herb 的 header-only 实现，
// Eigen 后端）。状态维度是编译期模板参数 —— 本模块只用 1 维（逐资产一条
// 随机游走均值），正好契合该约束。
#include "kalman/ExtendedKalmanFilter.hpp"
#include "kalman/LinearizedSystemModel.hpp"
#include "kalman/LinearizedMeasurementModel.hpp"

namespace mvo {

/**
 * @brief 均值估计器配置
 *
 * 口径对齐 service/lab/portfolio/pf/kalman.py（README §2.2~§2.5 已验证：
 * 卡尔曼均值 vs 样本均值，夏普 1.60 vs 1.27，样本外 1.39 vs 1.20）。
 *
 * 注意：lab 用的是 244 个 A 股交易日，这里统一走服务端的 YEAR_DAY(252)。
 */
struct MeanEstimatorConfig {
    MeanMethod _method = MeanMethod::Kalman;  // 均值估计方法

    // ── 卡尔曼 ──────────────────────────────────────────
    double _q_fast = 4e-6;           // 快滤波器过程噪声（随机游走的方差增量）
    double _q_slow = 4e-7;           // 慢滤波器过程噪声
    bool   _double_accuracy = true;  // 双精度融合（按后验方差逆方差加权）
    double _prior_mean = 0.0;        // 均值先验（收益率长期中枢）
    double _prior_var = 1e-4;        // 均值先验方差

    // ── 观测噪声 ────────────────────────────────────────
    double _obs_var_halflife = 60.0; // 残差收益 EWMA 的半衰期（交易日）

    // ── 因子剥离 ────────────────────────────────────────
    int    _factor_window = 120;     // 滚动 OLS 窗口
    double _beta_shrink = 0.10;      // beta 向 1.0 收缩的强度（0=不收缩）
    int    _factor_min_obs = 40;     // 滚动回归的最小有效样本

    // ── 样本均值（对照组）───────────────────────────────
    int    _sample_window = 60;      // 滚动样本均值窗口

    int    _trading_days = YEAR_DAY; // 年化因子
};

/**
 * @brief 均值估计结果
 */
struct MeanEstimate {
    std::vector<double> _mu_total;   // 年化总收益均值 (n) —— 送进 MVO 的 μ
    std::vector<double> _mu_alpha;   // 日频 alpha 均值 (n)
    std::vector<double> _t_stat;     // μ̂/√P 显著性 (n)
    std::vector<double> _betas;      // 滚动 beta (n)
    double _factor_mu = 0.0;         // 市场因子的日频均值
    int    _samples = 0;             // 已消费的样本数
    bool   _valid = false;
    String _note;                    // 诊断信息
};

/**
 * @brief 时变均值估计器：因子剥离 → 双精度卡尔曼 → 总收益还原
 *
 * 逐资产跑一维滤波器（观测噪声取对角阵时 n 个资产数学上完全解耦，
 * 一维与 n 维严格等价），换来数值稳定与 O(n) 成本。
 *
 * 在线用法：每根 bar 喂一次完整收益面板（行=时间，列=资产，NaN=缺失），
 * 内部只推进"尚未消费"的时间点，滤波器状态跨调用保持。
 * 若传入面板比已消费量短（历史被重置或修正），或已消费部分的数值发生变化
 * （右对齐错位 / 数据修正），自动从头重算。
 */
class MeanEstimator {
public:
    explicit MeanEstimator(const MeanEstimatorConfig& cfg = MeanEstimatorConfig());

    /// 重置滤波器与内部递推状态
    void reset();

    /// 换配置（会重置状态 —— 滤波器状态与配置强相关）
    void setConfig(const MeanEstimatorConfig& cfg);

    /// 推进估计器（幂等：重复喂同一面板不会重复推进）
    MeanEstimate update(const Eigen::MatrixXd& returns_history);

    const MeanEstimatorConfig& config() const { return _cfg; }

private:
    /// 一维随机游走滤波器（状态 = 隐含时变均值）
    ///
    /// 状态 x_t ∈ R，转移 F = 1（x_t = x_{t−1} + w_t, w ~ N(0,q)），
    /// 观测 H = 1（y_t = x_t + v_t, v ~ N(0,r_t)）。
    /// 观测噪声 r_t 逐日可变，所以每次 update 前都要重设测量模型的协方差。
    class ScalarKalman {
    public:
        using State = Kalman::Vector<double, 1>;
        using Meas  = Kalman::Vector<double, 1>;

        void init(double prior_mean, double prior_var);
        /// 一次观测更新；z 或 r 非有限/非正时只做预测（协方差增长），跳过观测
        void update(double z, double r, double q);

        double mean() const { return _kf.getState()(0); }
        double var()  const { return _kf.getCovariance()(0, 0); }

    private:
        // 随机游走系统模型：f(x,u) = x，F = W = I
        class RandomWalkSystem
            : public Kalman::LinearizedSystemModel<State, State> {
        public:
            using Base = Kalman::LinearizedSystemModel<State, State>;
            using Base::Control;
            using Base::State;
            State f(const State& x, const Control&) const override { return x; }
        };

        // 单位观测模型：h(x) = x，H = V = I
        class IdentityMeasurement
            : public Kalman::LinearizedMeasurementModel<State, Meas> {
        public:
            using Base = Kalman::LinearizedMeasurementModel<State, Meas>;
            using Base::Measurement;
            using Base::State;
            Measurement h(const State& x) const override { return x; }
        };

        Kalman::ExtendedKalmanFilter<State> _kf;
        RandomWalkSystem      _sys;
        IdentityMeasurement   _meas;
    };

    /// 快慢双滤波器，按后验方差做逆方差加权融合
    struct FilterPair {
        ScalarKalman _fast;
        ScalarKalman _slow;
    };

    MeanEstimatorConfig _cfg;

    int    _n = 0;             // 资产数
    int    _consumed = 0;      // 已消费的时间点数
    ScalarKalman _factor;      // 市场因子自身的滤波器（q 用 q_fast 的 10 倍）

    std::vector<FilterPair> _filters;   // 逐资产滤波器
    std::vector<double>      _ewmaMean;  // 逐资产残差收益的 EWMA 一阶矩（观测噪声用）
    double                   _factorEwmaMean = std::numeric_limits<double>::quiet_NaN();
    MeanEstimate             _last;      // 最近一次的估计结果（幂等返回用）
    Eigen::MatrixXd          _lastPanel; // 已消费的收益面板（用于检测历史变更）

    /// 滚动 OLS beta（截距=0），样本不足或因子退化时返回 1.0
    double rollingBeta(int t, int i,
                       const Eigen::MatrixXd& hist,
                       const std::vector<double>& factor) const;

    /// 滚动样本均值（对照组口径）：窗口内有效值的算术平均
    double rollingSampleMean(int t, int i,
                             const Eigen::MatrixXd& hist) const;
};

}  // namespace mvo
