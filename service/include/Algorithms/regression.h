#pragma once

/// 通用金融回归内核
/// ────────────────────────────────────────────────────────────────────────────
/// 一套估计 + 推断的核心，供三个消费面共用：
///   1. 图内节点（ResidualizeNode）——在线横截面中性化
///   2. REST 接口（/v0/analysis/regression）——交互式分析
///   3. Python 训练侧（tools/ml_regression.py）——批量评估
///
/// 分层（重要）：
///   Algorithms/regression.{h,cpp}     纯数学内核，只依赖 Eigen + std。
///                                     **不引入 JSON**——M3 的图内节点只用
///                                     PanelData + 三个函数，不该背序列化依赖。
///   Util/Spec/regression_spec.{h,cpp} JSON ⇄ PanelData 的编解码与 spec 校验，
///                                     依赖 json.hpp + 本头文件。
///   Handler/RegressionHandler         httplib 薄包装（解析 body → 调 spec → 写响应）。
///
/// 三类估计（估计单元不同，不能混在一个接口里）：
///   CrossSection  每个交易日 t，跨 N 个标的：y[i,t] = a_t + X[i,t]'b_t + e[i,t]
///                 产出逐日因子收益 b_t 与残差 e[i,t]（中性化的直接产物）
///   TimeSeries    每个标的 i，跨 T 期：y[i,t] = a_i + F[t]'b_i + e[i,t]
///                 产出逐标的因子暴露 b_i 与 t 值（风险模型）
///   Panel         混合固定效应：y[i,t] = a_i + g_t + X'beta + e
///                 产出平均效应 beta
///
/// 推断层与估计层解耦：SeType 决定标准误口径。
///
/// 口径约定（Python 侧 tools/ml_stats.py 必须逐字对齐，parity 测试守着这条线）：
///   listwise 删除：y 或任一 X 含非有限值 → 整行丢弃，计数写入 Fit::_n_dropped_rows。
///                  绝不静默插补。
///   秩揭示：设计矩阵按列顺序贪心保留（ColPivHouseholderQR 判秩），
///          被判线性相关的列**显式剔除**并记录到 Term::_dropped / _drop_reason，
///          绝不让最小二乘返回一组无意义的系数。
///   IID 标准误：V = sigma^2 * (X'X)^-1，sigma^2 = e'e / (n - rank)
///   HAC 标准误：Omega = sum_t x_t u_t^2 x_t'
///                    + sum_{j=1..L} w_j sum_{t>j} (x_t u_t x_{t-j}' u_{t-j}
///                                             + x_{t-j} u_{t-j} x_t' u_t)
///                w_j = 1 - j/(L+1)（Bartlett，与 finance.cpp / ml_stats 同族）
///                V = (X'X)^-1 Omega (X'X)^-1
///   聚类标准误：V = c * (X'X)^-1 (sum_g X_g' u_g u_g' X_g) (X'X)^-1
///                c = G/(G-1) * (n-1)/(n-rank)
///   t 分布 p 值：p = I_{df/(df+t^2)}(df/2, 1/2)，df = n - rank
///                I_x 为正则化不完全 Beta（连分式展开，见 implementation）
///   岭回归：(X'X + lambda*I) beta = X'y；常量列（截距）不参与惩罚
///   FWL 部分残差化：先按分组去均值（等价于哑变量回归的拟合值），
///                  再对去均值后的 X 回归取残差——数学上与联合回归残差等价，
///                  但不构造哑变量矩阵，小组样本下条件数好得多。
///   系数序列的均值检验（横截面逐日系数 / 时序逐标的暴露）：
///                mean = (1/n) sum x_t，Var(mean) = S/n，
///                S = gamma0 + 2 sum_{j=1..L} w_j gamma_j（gamma 为自协方差），
///                L = 0 时退化为 IID 的 Var(mean) = 样本方差/n。
///
/// 本头文件刻意不 include std_header.h（只依赖 Eigen + std）：
///   一是让内核可以被单独编译/单测，二是避免把 spdlog 之类的重头文件
///   拖进每一个使用者。类型一律用 std::string / std::vector / std::map。

#include <Eigen/Dense>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace regression {

using Mat = Eigen::MatrixXd;
using Vec = Eigen::VectorXd;

/// 标准误口径
enum class SeType {
    IID,      ///< 同方差
    HAC,      ///< Newey-West 自相关稳健
    Cluster,  ///< 聚类稳健（聚类由调用方给 id：日期/标的/分组均可）
};

/// 估计模式
enum class Mode {
    CrossSection,  ///< 逐日横截面
    TimeSeries,    ///< 逐标的时序
    Panel,         ///< 双向固定效应面板
};

/// 拟合选项（样本守卫集中在这里，不再散落到各个调用点）
struct FitOptions {
    int         _min_obs        = 10;     ///< 每次回归的最少有效样本
    int         _min_group_size = 2;      ///< 分组最少成员数，不足则并入 fallback
    int         _hac_lag        = 20;     ///< HAC 滞后阶
    bool        _add_const      = true;   ///< 是否加截距
    double      _winsor_q       = 0.0;    ///< 截面缩尾分位（0 = 不缩尾）
    double      _ridge_lambda   = 0.0;    ///< 岭惩罚（0 = OLS）
    bool        _report_vif     = true;   ///< 是否计算 VIF
    bool        _time_ordered   = false;  ///< 行是否按时间排列（Durbin-Watson 才有意义）
    std::string _fallback_group = "OTHER";///< 小组合并后的兜底组名
};

/// 单个回归项
struct Term {
    std::string _name;
    double      _beta     = 0.0;
    double      _se       = 0.0;
    double      _t        = 0.0;
    double      _p        = 1.0;
    double      _std_beta = 0.0;  ///< 标准化系数（仅对数值列有意义）
    bool        _dropped  = false;
    std::string _drop_reason;
};

/// 一次回归的完整结果
struct Fit {
    int    _n_obs          = 0;
    int    _n_reg          = 0;   ///< 设计矩阵列数（含被剔除的）
    int    _rank           = 0;   ///< 有效秩
    int    _n_dropped_rows = 0;   ///< 因非有限值被 listwise 丢弃的行数
    double _r2             = 0.0;
    double _adj_r2         = 0.0;
    double _sigma          = 0.0;
    double _dw             = 0.0; ///< Durbin-Watson（_time_ordered=false 时为 0）
    double _cond           = 0.0; ///< 设计矩阵（列标准化后）的条件数
    std::vector<Term>        _terms;
    std::vector<double>      _vif;   ///< 与 _terms 对齐；截距与被剔除项为 0
    std::vector<std::string> _warnings;
};

/// 面板数据：一次回归需要的所有原料
///
/// X 是**截面因子**（T×N，每个因子一张面板，例如 log 市值、估值偏离）
/// F 是**时序因子**（长度 T，全体标的共享，例如市场组合收益、宏观变量）
struct PanelData {
    std::vector<std::string> _dates;    ///< T
    std::vector<std::string> _symbols;  ///< N
    Mat                      _Y;        ///< T×N，被解释变量
    std::map<std::string, Mat> _X;      ///< 截面因子：T×N
    std::map<std::string, Vec> _F;      ///< 时序因子：T
    /// 分组变量：变量名 -> 每个标的的组标签（长度 N）
    std::map<std::string, std::vector<std::string>> _groups;
};

/// 横截面回归结果
struct CrossSectionResult {
    std::vector<std::string> _dates;
    Mat                      _beta;     ///< T×K，逐日系数
    Mat                      _resid;    ///< T×N，逐日残差（中性化产物）
    Vec                      _r2;       ///< T，逐日 R²
    std::vector<int>         _n_names;  ///< T，逐日有效标的数
    std::vector<int>         _dropped;  ///< T×K，该日被剔除的列（1 = 剔除）
    std::vector<Term>        _terms;    ///< 全局项定义（K 个），系数为逐日均值
    std::vector<double>      _vif;      ///< 与 _terms 对齐（全样本口径）
    double                   _cond      = 0.0;
    std::vector<std::string> _warnings;
};

/// 时序回归结果
struct TimeSeriesResult {
    std::vector<std::string> _symbols;
    Mat                      _beta;     ///< N×K，逐标的系数
    Mat                      _se;       ///< N×K，逐标的系数标准误
    Mat                      _tstat;    ///< N×K，逐标的 t 值
    Vec                      _r2;       ///< N
    std::vector<int>         _n_obs;    ///< N
    std::vector<int>         _dropped;  ///< N×K
    std::vector<Term>        _terms;    ///< 截面平均暴露
    std::vector<double>      _vif;
    double                   _cond      = 0.0;
    std::vector<std::string> _warnings;
};

/// 面板回归结果
struct PanelResult {
    std::vector<Term>        _terms;
    double                   _r2      = 0.0;
    double                   _adj_r2  = 0.0;
    int                      _n_obs   = 0;
    std::vector<std::string> _warnings;
};

// ════════════════════════════════════════════════════════════════════════════
// 核心估计
// ════════════════════════════════════════════════════════════════════════════

/// 单次 OLS / 岭回归
///
/// @param y       被解释变量（n）
/// @param X       设计矩阵（n×k），列顺序与 names 一致；截距列由调用方决定放哪
/// @param names   列名（长度 k）；为空时自动生成 x0..xk-1
/// @param opt     拟合选项
/// @param se      标准误口径
/// @param cluster 聚类 id（长度 n，仅 se == Cluster 时使用；空则退化为 IID）
Fit ols(const Vec& y, const Mat& X, const std::vector<std::string>& names,
        const FitOptions& opt, SeType se, const std::vector<int>& cluster);

/// 逐日横截面回归
///
/// 设计矩阵 = [截距] + X 面板列 + F 时序列 + 分组哑变量
/// 哑变量用参照组编码（加截距时丢掉排序最前的一级），全局确定列集合，
/// 逐日再做一次局部秩判定，某日不可估的列在该日填 NaN 并在 _dropped 标记。
///
/// @param groupnames 分组变量名（取自 PanelData::_groups）
CrossSectionResult crossSection(const PanelData& p,
                                const std::vector<std::string>& xnames,
                                const std::vector<std::string>& fnames,
                                const std::vector<std::string>& groupnames,
                                const FitOptions& opt, SeType se);

/// 逐标的时序回归
///
/// 设计矩阵 = [截距] + F 时序列 + 该标的自身的 X 面板列
TimeSeriesResult timeSeries(const PanelData& p,
                            const std::vector<std::string>& xnames,
                            const std::vector<std::string>& fnames,
                            const FitOptions& opt, SeType se);

/// 双向固定效应面板回归
///
/// 先按日期与标的交替去均值（等价于吸收两组固定效应），再对 X 做无截距 OLS。
/// R² 为组内（within）R²。
PanelResult panel(const PanelData& p,
                  const std::vector<std::string>& xnames,
                  const FitOptions& opt, SeType se);

// ════════════════════════════════════════════════════════════════════════════
// 部分残差化（FWL）
// ════════════════════════════════════════════════════════════════════════════

/// 交替投影去均值（就地修改 y 与 X 各列）
///
/// 分组去均值 = 哑变量回归的拟合值；多分组变量时交替投影迭代到不动点，
/// 单分组变量时一步即精确。
///
/// @param codes   每个分组变量一组组号（长度 = 行数）
/// @param nGroups 每个分组变量的组数
void alternateDemean(Vec& y, Mat& X,
                     const std::vector<std::vector<int>>& codes,
                     const std::vector<int>& nGroups);

/// FWL 部分残差：等价于 y ~ 1 + 分组 + X 的联合回归残差
///
/// 先按分组去均值（y 与 X 各列），再对去均值后的 X 做 OLS 取残差。
Vec partialResidual(const Vec& y, const Mat& X,
                    const std::vector<std::vector<int>>& codes,
                    const std::vector<int>& nGroups, bool addConst);

// ════════════════════════════════════════════════════════════════════════════
// 工具
// ════════════════════════════════════════════════════════════════════════════

/// 把组标签转成组号；成员数 < minSize 的组并入 fallback 组名
/// @return 组号（长度 = labels.size()）；组号 k 对应的标签写入 outLabels
std::vector<int> buildGroupCodes(const std::vector<std::string>& labels, int minSize,
                                 const std::string& fallback,
                                 std::vector<std::string>& outLabels);

/// 截面缩尾（按分位裁剪），就地修改
void winsorizeCrossSection(Vec& values, double q);

/// 正则化不完全 Beta 函数 I_x(a, b)
double regularizedIncompleteBeta(double a, double b, double x);

/// 双侧 student-t p 值：p = I_{df/(df+t^2)}(df/2, 1/2)
double studentTTwoSidedP(double t, double df);

}  // namespace regression
