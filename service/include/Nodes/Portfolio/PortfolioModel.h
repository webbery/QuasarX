#pragma once
#include "std_header.h"
#include "DataContext.h"
#include "json.hpp"
#include <functional>
#include <memory>

namespace portfolio {

/**
 * @brief 组合（仓位）计算方法
 *
 * 每种方法对应一个 PortfolioModel 子类。PortfolioNode 在 Init 时按配置只
 * 创建**被选中的那一个**（unique_ptr），未选中的模型连同它的递推状态
 * （卡尔曼滤波器、EWMA 协方差、上期权重……）都不会常驻内存。
 */
enum class SizingMethod: char {
    Equal,              ///< 等权分配（默认，保持原有行为）
    Kelly,              ///< Kelly 公式
    VolatilityTarget,   ///< 目标波动率反推
    MVO,                ///< 均值方差模型（卡尔曼均值 + OSQP）
    BlackLitter,        ///< Black-Litterman 模型（未实现）
    Strength            ///< 信号强度加权（读取 {symbol}.strength）
};

/// 枚举 → 配置字符串（"equal" / "kelly" / "volatility_target" / "mvo" ...）
String to_string(SizingMethod method);

/// 配置字符串 → 枚举（无法识别时回退 Equal）
SizingMethod parseSizingMethod(const String& text);

/**
 * @brief 一条待建仓的腿 —— 做多/做空统一
 *
 * 只表示"这一根 bar 要新开的仓"。平仓腿不占预算，不进这张表。
 * 方向由信号决定（BUY → +1，SELL → −1）；模型不改方向，只定量。
 *
 * 组（定仓单元）由 `_group` + 腿表连续存放表达：同一个非 0 `_group` 的
 * 连续腿是一组；`_group == 0` 的腿自成一组（独立多空就是每条腿一组）。
 * 配对 = 2 条腿、方向相反、share 由 β 决定的组。
 */
struct PortfolioLeg {
    symbol_t _symbol = null_symbol();
    float    _share  = 1.0f;   ///< 组内名义份额（组内 Σ = 1；独立腿 = 1）
    /// 定仓组 id（稠密，0 = 独立腿）。节点每根 bar 按组出现顺序分配，
    /// 组级统计键由 PortfolioContext::_group_keys 给出。
    unsigned _group  : 16 = 0;
    int      _dir    : 2  = 0; ///< +1 做多 / −1 做空
};

/**
 * @brief 组合计算输入 —— 只有"所有 sizing 模型都成立"的东西
 *
 * 模型专属数据不在里面：
 *   - 凯利的胜率/赔率/样本数 → KellyModel 自己从 _data 读
 *   - MVO 的 universe / 上期权重 / 卡尔曼状态 → MvoModel 自己的成员
 */
struct PortfolioContext {
    DataContext&                       _data;
    Vector<PortfolioLeg>               _legs;        ///< 要建仓的腿，按组连续存放
    /// 组 id → 组级统计键（index 0 占位；独立腿的键 = 标的代码，不走这张表）。
    /// 组 id 是节点分配的稠密索引，而统计键里带的是生产者发布的原始 id
    /// （如 pair_id），两者由节点在这一层做映射。
    Vector<String>                     _group_keys;
    std::function<void(const String&)> _warn;        ///< 告警回调（live / backtest 分流由节点决定）

    void warn(const String& msg) const {
        if (_warn) _warn(msg);
    }
};

/**
 * @brief 组统计键：有组 id → "group.<id>"，独立腿 → 标的代码
 *
 * 生产者（PairNode / 篮子节点 / 任何策略节点）按同一口径发布组级统计，
 * 消费方（Kelly / 目标波动率）按同一口径读取。只在这里定义一次。
 */
inline String groupKey(uint32_t group_id, symbol_t symbol) {
    if (group_id == 0) {
        return get_symbol(symbol);
    }
    return String("group.") + std::to_string(group_id);
}

/// 腿所属组的统计键：独立腿 = 标的代码；成组腿 = 节点为组分配的键
inline String groupKeyOf(const PortfolioContext& ctx, const PortfolioLeg& leg) {
    if (leg._group == 0) {
        return get_symbol(leg._symbol);
    }
    if (leg._group < ctx._group_keys.size()) {
        return ctx._group_keys[leg._group];
    }
    return String();
}

/**
 * @brief 组的腿区间
 *
 * 约定：腿表按组连续存放；同 `_group` 的连续腿是一组；`_group == 0` 的腿
 * 各自成组（独立多空就是"每条腿一个组"）。
 *
 * @return offsets，长度 G+1；组 g 的腿 = legs[offsets[g], offsets[g+1])。
 *         腿表为空时返回空数组（G = 0）。
 */
Vector<size_t> groupOffsets(const Vector<PortfolioLeg>& legs);

/// 组数（= groupOffsets(legs).size() - 1；空腿表为 0）
size_t groupCount(const Vector<PortfolioLeg>& legs);

/**
 * @brief 组合计算模型基类
 *
 * `compute()` 是**模板方法**，模型只实现 `computeScales()`：
 *
 *     computeScales()  → 每个定仓单元一个尺度（占预算的比例）
 *     distribute()     → w_leg = scale[group] × share_leg
 *     applyCaps()      → 单腿上限 + 总敞口 / 净敞口上限
 *
 * 返回空尺度 = 模型不介入 → 基类按 1/G 等权兜底。因此 `compute()` 永远
 * 返回**覆盖全部建仓腿**的权重表，调用方不再需要"缺权重时按决策数均摊"
 * 的兜底分支。
 */
class PortfolioModel {
public:
    virtual ~PortfolioModel() = default;

    /// 解析公共参数（单腿 / 总敞口 / 净敞口上限）后交给子类解析自己的参数。
    /// universe 是节点交易池 —— 只有需要它的模型（目前是 MVO 的因子剥离）
    /// 覆写 onUniverse 保存，其余模型忽略。
    bool Init(const nlohmann::json& config, const Set<symbol_t>& universe = {});

    /// 每次运行开始时的状态重置（无状态模型不用管）
    virtual void Prepare() {}

    /// 计算权重（占预算比例），覆盖全部建仓腿
    Map<symbol_t, double> compute(PortfolioContext& ctx);

    /// 诊断名（NODE_IO_LOG 用）
    virtual const char* name() const = 0;

    /// 本模型对应的方法枚举
    SizingMethod method() const { return _method; }

protected:
    /// 子类解析自己的参数
    virtual bool onInit(const nlohmann::json& /*config*/) { return true; }

    /// 需要交易池的模型覆写（目前只有 MVO）
    virtual void onUniverse(const Set<symbol_t>& /*universe*/) {}

    /// 模型唯一要实现的东西：每个定仓单元一个尺度（size = G）。
    /// 返回空 = 不介入 → 基类按等权兜底。
    virtual Vector<double> computeScales(PortfolioContext& ctx) = 0;

    /// 尺度 → 腿权重：w_leg = scales[g] × share_leg
    static Map<symbol_t, double> distribute(const PortfolioContext& ctx,
                                            const Vector<double>& scales);

    /// 单腿上限 + 总敞口（Σ|w|）+ 净敞口（|Σ dir·w|）截断
    Map<symbol_t, double> applyCaps(Map<symbol_t, double> weights,
                                    const PortfolioContext& ctx) const;

    /// 输出是否走 applyCaps（MVO 的 QP 已带 0 ≤ w ≤ w_max 与 Σw = e）
    virtual bool capsOnOutput() const { return true; }

    SizingMethod _method = SizingMethod::Equal;
    float _max_single_pct = 1.0;   // 单腿上限（默认不限制）
    float _max_gross_pct  = 1.0;   // 总敞口上限 Σ|w|（默认不限制）
    float _max_net_pct    = 0.0;   // 净敞口上限 |Σ dir·w|，0 = 不限制
};

/// 工厂：按方法创建对应的模型（只创建被选中的那一个）
std::unique_ptr<PortfolioModel> createPortfolioModel(SizingMethod method);

}  // namespace portfolio
