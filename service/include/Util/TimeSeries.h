#pragma once
#include "std_header.h"
#include "Util/finance.h"  // ADFResult / KPSSResult / EGFullResult / JohansenResult
#include <Eigen/Dense>

/**
 * 时间序列通用计算
 *
 * 入参一律是 Eigen 类型（Eigen::VectorXd 单序列 / Eigen::MatrixXd 多序列矩阵，
 * 约定 N 行 × T 列：行 = 变量，列 = 时间点），不知道标的、OHLCV 字段、频率这些业务概念
 * ——取数、命名、序列化都归调用方。
 *
 * 分层:
 *   - Handler/TimeSeriesHandler.cpp  取数 + HTTP 编解码（参数解析 / JSON 序列化 / 命名）
 *   - Util/TimeSeries.{h,cpp}         本模块：检验编排（Eigen ↔ finance 内核的类型适配）
 *   - Util/finance.{h,cpp}            计量检验内核（ADF/KPSS/Johansen/临界值/p 值 等）
 *
 * TODO(剥离重构计量检验工具):
 *   finance.cpp（2378 行）目前把「金融工具」（Ledoit-Wolf 收缩、风险平价、收益/波动/夏普）
 *   与「计量检验内核」（adfTestFull / kpssTest / engleGrangerFull / johansenTest + johansenCV /
 *   grangerCausalityTest / multivariateGrangerTest / mackinnonCV + mackinnonPValue /
 *   fDistributionCDF / chiSquaredPValue）混在同一个文件里，而后者只服务时序分析。
 *   目标: 把计量检验内核剥离到本模块（或其下的独立子模块），让 finance.cpp 回归
 *   「金融工具」定位。本模块届时只需调整 include 与命名空间，不改调用点语义。
 *   详见 service/doc/time_series.md。
 */

namespace timeseries {

/// 平稳性检验（ADF + 互补的 KPSS）
struct UnitRoot {
    finance::ADFResult _adf;
    finance::KPSSResult _kpss;
};
UnitRoot ComputeUnitRoot(const Eigen::VectorXd& values);

/// 协整回归残差
struct ResidualPair {
    Eigen::VectorXd _residuals;
};
ResidualPair ComputeResiduals(const Eigen::VectorXd& x, const Eigen::VectorXd& y);

/// Engle-Granger 两步法
/// 结果里的 _symbol_x / _symbol_y 留空，由调用方按需标注
finance::EGFullResult ComputeEngleGranger(const Eigen::VectorXd& x, const Eigen::VectorXd& y);

/// Johansen 协整检验
/// 迹统计量与最大特征值统计量同源，一次计算两用
finance::JohansenResult ComputeJohansen(const Eigen::MatrixXd& data);

/// 逐对 Granger 因果（二元 F 检验，每对两个方向）
/// _cause_index / _effect_index 对应矩阵的行下标
struct GrangerPair {
    size_t _cause_index = 0;
    size_t _effect_index = 0;
    double _f_statistic = 0;
    double _p_value = 1;
    bool _is_significant = false;   ///< p < 0.05
    int _optimal_lag = 0;
};
Vector<GrangerPair> ComputePairwiseGranger(const Eigen::MatrixXd& data, int max_lag);

/// 多元 Granger 因果（VAR + Wald 检验，≥3 行）
struct MultivariateGranger {
    size_t _cause_index = 0;
    size_t _effect_index = 0;
    double _wald_stat = 0;
    double _p_value = 1;
    int _optimal_lag = 0;
    bool _is_significant = false;
    Vector<size_t> _condition_indices;
};
Vector<MultivariateGranger> ComputeMultivariateGranger(const Eigen::MatrixXd& data, int max_lag);

} // namespace timeseries
