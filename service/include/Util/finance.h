#pragma once
#include "std_header.h"
#include <Eigen/Dense>

namespace finance {

/**
 * @brief 三阶段增长模型
 */
double stage3GM(double g1, double g2, double D, double T1, double T2, double r);

/**
 * @brief 计算 Kyle's Lambda（订单流对价格的冲击系数）
 */
double kyles_lambda(const Vector<double>& prices,
                    const Vector<int64_t>& volumes,
                    int trade_side,
                    int64_t trade_volume);

/**
 * @brief 计算 Amihud 不流动性指标
 */
double amihud_illiquidity(const Vector<double>& prices,
                          const Vector<int64_t>& volumes);

// ──────────────────────────────────────────────────────────────────────
// 时间序列分析工具函数（多标的）
// ──────────────────────────────────────────────────────────────────────

/// OLS 回归结果
struct OLSResult {
    double alpha;              // 截距
    double beta;               // 斜率
    Vector<double> residuals;  // 残差序列
    double r_squared;          // R²
    double std_error;          // 回归标准误差
};

/// 交叉相关分析结果
struct CrossCorrelationResult {
    Vector<double> ccf;        // lag -max_lag ~ +max_lag 的相关系数
    int max_lag_index;         // CCF 向量中最大相关的索引
    double max_correlation;    // 最大 |相关系数|
    int lead_lag;              // >0: y领先x, <0: x领先y
};

/// 格兰杰因果检验结果
struct GrangerCausalityResult {
    double f_statistic;
    double p_value;
    bool is_significant;       // p < 0.05
    int optimal_lag;           // AIC 最小的滞后阶数
    String direction;          // "X→Y" 或 "Y→X"
};

/// 协整检验结果 (Engle-Granger)
struct CointegrationResult {
    double beta;               // y = α + βx + ε
    double alpha;
    double adf_statistic;      // 残差 ADF 检验统计量
    double p_value;
    bool is_cointegrated;      // p < 0.05
    double half_life;          // 均值回归半衰期 (bar 数)
};

/// ADF 检验结果 (含 MacKinnon 临界值)
struct ADFResult {
    double _statistic = 0;     // ADF t-统计量
    double _p_value = 1;       // MacKinnon p 值
    double _cv_1pct = 0;       // 1% 临界值
    double _cv_5pct = 0;       // 5% 临界值
    double _cv_10pct = 0;      // 10% 临界值
    int _lags = 0;             // 使用的增广滞后阶数
    bool _is_stationary = false; // p < 0.05
};

/// KPSS 检验结果
struct KPSSResult {
    double _statistic = 0;     // η 统计量
    double _p_value = 1;       // 近似 p 值
    int _lags = 0;             // Newey-West 带宽
    double _lr_variance = 0;   // 长期方差 (Bartlett kernel)
    bool _is_stationary = true; // H0: 平稳, p > 0.05
};

/// OU 过程 MLE 拟合结果
/// dX = θ(μ - X)dt + σdW
struct OUProcessResult {
    double _theta = 0;         // 均值回复速度
    double _mu = 0;            // 长期均值
    double _sigma = 0;         // 波动率
    double _half_life = 0;     // ln(2)/θ
    double _log_likelihood = 0;// 对数似然
    double _aic = 0;           // 2k - 2lnL
    double _se_theta = 0;      // 标准误
    double _se_mu = 0;
    double _se_sigma = 0;
};

/// Engle-Granger 两步法完整结果
struct EGFullResult {
    String _symbol_x, _symbol_y;
    // Step 1: 协整回归
    double _alpha = 0, _beta = 0;
    double _r_squared = 0;
    // Step 2: 残差平稳性检验
    ADFResult _adf;
    KPSSResult _kpss;
    double _half_life = 0;     // 从 AR(1) 系数反推
    bool _is_cointegrated = false;
    // OU 过程拟合
    OUProcessResult _ou_fit;
    // 残差序列 (前端画图用)
    Eigen::VectorXd _residuals;
};

/// Johansen 协整检验结果
struct JohansenResult {
    int _n_variables = 0;
    // trace 统计量 (r=0, r≤1, ..., r≤n-1)
    Eigen::VectorXd _trace_stats;
    Eigen::VectorXd _trace_cv_95;
    Eigen::VectorXd _trace_cv_99;
    Vector<bool> _trace_significant;
    // max-eigen 统计量
    Eigen::VectorXd _max_eigen_stats;
    Eigen::VectorXd _max_eigen_cv_95;
    Eigen::VectorXd _max_eigen_cv_99;
    Vector<bool> _max_eigen_significant;
    // 协整向量矩阵 (N × N, 列=特征向量)
    Eigen::MatrixXd _eigenvectors;
    int _rank = 0;             // 估计的协整秩 (5% 显著性)
};

/// 多元 Granger 因果检验结果 (VAR + Wald)
struct MultivariateGrangerResult {
    String _from, _to;
    double _wald_stat = 0;     // Wald 统计量
    double _p_value = 1;       // χ² 分布 p 值
    int _optimal_lag = 0;      // AIC 选择的最优滞后
    bool _is_significant = false; // p < 0.05
    Vector<String> _condition_set; // 条件集 (其他变量)
};

/// OLS 回归: y = α + βx + ε
OLSResult olsRegression(const Vector<double>& x, const Vector<double>& y);

/// 交叉相关函数 (Cross-Correlation Function)
/// 计算 x 和 y 在滞后 [-max_lag, +max_lag] 下的相关系数
/// 正值 lag: y 领先 x；负值 lag: x 领先 y
CrossCorrelationResult crossCorrelation(
    const Vector<double>& x, const Vector<double>& y, int max_lag);

/// 格兰杰因果检验
/// 检验 y 是否是 x 的格兰杰原因
GrangerCausalityResult grangerCausalityTest(
    const Vector<double>& x, const Vector<double>& y, int max_lag,
    const String& x_name = "X", const String& y_name = "Y");

/// Engle-Granger 协整检验
/// 检验 x 和 y 是否存在长期均衡关系
CointegrationResult engleGrangerTest(
    const Vector<double>& x, const Vector<double>& y);

// ──────────────────────────────────────────────────────────────────────
// 协整分析增强：ADF (MacKinnon) / KPSS / OU / Johansen / 多元Granger
// ──────────────────────────────────────────────────────────────────────

/// ADF 检验 (完整 MacKinnon 临界值 + p 值)
/// reg_type: "c" = 截距, "ct" = 截距+趋势, "nc" = 无截距无趋势
ADFResult adfTestFull(const Vector<double>& series, int max_lag = -1,
                       const String& reg_type = "c");

/// KPSS 平稳性检验
/// H0: 序列平稳; lags=-1 自动选 (Schwert 1989)
/// reg_type: "level" = 水平检验, "trend" = 趋势平稳检验
KPSSResult kpssTest(const Vector<double>& series, int lags = -1,
                     const String& reg_type = "level");

/// OU 过程 MLE 拟合: dX = θ(μ - X)dt + σdW
/// dt: 采样间隔 (默认 1.0)
OUProcessResult fitOUProcess(const Eigen::VectorXd& x, double dt = 1.0);

/// Engle-Granger 两步法完整结果 (含 OU 拟合 + 残差序列)
EGFullResult engleGrangerFull(const Vector<double>& x, const Vector<double>& y,
                               const String& symbol_x = "X",
                               const String& symbol_y = "Y");

/// Johansen 协整检验 (多元)
/// data: N 行 × T 列 (行=标的, 列=时间点)
/// detrend: "none" / "const" / "trend"
JohansenResult johansenTest(const Eigen::MatrixXd& data, int lag = 1,
                             const String& detrend = "const");

/// 多元 Granger 因果检验 (VAR + Wald)
/// data: N 行 × T 列
Vector<MultivariateGrangerResult> multivariateGrangerTest(
    const Eigen::MatrixXd& data,
    const Vector<String>& symbols,
    int max_lag = 10);

// ──────────────────────────────────────────────────────────────────────
// 协方差收缩 + 投资组合优化
// ──────────────────────────────────────────────────────────────────────

/// Ledoit-Wolf 收缩结果 (Ledoit-Wolf OAS 闭式, Chen-Wiesel-Eldar-Hero 2010)
struct LedoitWolfResult {
    Eigen::MatrixXd _covariance;        // (N x N) 收缩后协方差
    double _shrinkage = 0;              // δ ∈ [0, 1]
    int _n_observations = 0;            // 时间点 T (列数)
    int _n_variables = 0;               // 标的数 N (行数)
};

/// Risk Parity 求解结果 (Qian 2005 Spin-Glass 迭代)
struct RiskParityResult {
    Eigen::VectorXd _weights;                // 归一化权重, Σw = 1
    Eigen::VectorXd _risk_contributions;     // RC_i = w_i × (Σw)_i
    double _max_rc_deviation = 0;            // max |RC_i / RC̄ - 1|, < tolerance 收敛
    int _iterations = 0;                     // 迭代步数
    bool _converged = false;
};

/// Ledoit-Wolf 协方差收缩 (Ledoit-Wolf OAS 闭式)
/// 返回的 _covariance 是线性收缩结果: (1-δ)·S + δ·F
/// F = (tr(S)/N)·I (常数方差目标)
/// 输入 returns: N 行 × T 列 (每行一个标的，每列一个时间点)
LedoitWolfResult ledoitWolfShrinkage(const Eigen::MatrixXd& returns);

/// Risk Parity 权重 (Qian 2005 Spin-Glass 迭代)
/// 求解 w_i·(Σw)_i = const 等风险贡献, 协方差由 ledoitWolfShrinkage 提供
/// 输入 returns: N 行 × T 列
RiskParityResult riskParityWeights(const Eigen::MatrixXd& returns,
                                    double tolerance = 1e-6,
                                    int max_iterations = 200);

// ──────────────────────────────────────────────────────────────────────
// 信号分析 / 时序分析工具函数
// ──────────────────────────────────────────────────────────────────────

/**
 * @brief 计算自相关函数 (ACF)
 * @param data     输入序列
 * @param max_lag  最大滞后阶数
 * @return         ACF 值，索引 0 = lag 0（总是 1.0）
 */
Vector<double> computeACF(const Vector<double>& data, int max_lag);

/**
 * @brief 计算偏自相关函数 (PACF)，使用 Durbin-Levinson 算法
 * @param acf      自相关函数值（从 computeACF 获得）
 * @param max_lag  最大滞后阶数
 * @return         PACF 值，索引 0 = lag 0
 */
Vector<double> computePACF(const Vector<double>& acf, int max_lag);

/**
 * @brief 估计序列的平均周期（ACF 第一个过零点 × 2）
 * @param data  输入序列
 * @return      平均周期（bar 数），无法估计时返回 0
 */
double estimateMeanPeriod(const Vector<double>& data);

/**
 * @brief 计算能量占比（序列方差 / 原始信号方差）
 * @param component  分量序列（如 IMF）
 * @param original   原始信号序列
 * @return           能量占比 [0, 1]
 */
double computeEnergyPct(const Vector<double>& component,
                         const Vector<double>& original);

/**
 * @brief 滚动 EMD 能量计算
 *
 * 在每个窗口位置 t ∈ [window-1, N) 对 data[t-window+1:t+1] 运行 EMD，
 * 返回每个窗口下 (numIMFs 个 IMF + 残差) 的能量占比时间序列。
 *
 * @param data       原始信号
 * @param window     滚动窗口大小（bar 数，建议 30-120）
 * @param numIMFs    每个窗口 EMD 分解的 IMF 数量
 * @param dates      完整日期序列，与 data 等长；返回时输出每个窗口对应的结束日期
 * @return           每个窗口的能量占比：外层索引 = IMF i (0..numIMFs-1)，最后一项 = 残差；
 *                   每个内层 vector 长度为 N - window + 1
 */
Vector<Vector<double>> computeRollingEMDEnergy(const Vector<double>& data,
                                                int window,
                                                int numIMFs,
                                                const Vector<String>& dates);

/**
 * @brief EWMA 波动率标准化
 *
 * 将收益率序列除以 EWMA 条件波动率，得到标准化序列 z_t = r_t / sigma_t。
 * 标准化后的序列近似 N(0,1)，消除了波动率时变的影响。
 *
 * EWMA 方差递推：sigma_t^2 = decay * sigma_{t-1}^2 + (1 - decay) * r_{t-1}^2
 *
 * @param returns  原始收益率序列
 * @param decay    EWMA 衰减系数（典型值 0.94，RiskMetrics 推荐）
 * @return         标准化收益率序列（与输入等长）
 */
Vector<double> ewmaVolatilityStandardize(const Vector<double>& returns, double decay = 0.94);

}

bool LoadStockQuote(DataFrame& df, const String& path);