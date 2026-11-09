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
enum class SizingMethod {
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

// ── 配置读取（参数统一使用英文键）──
double readNumber(const nlohmann::json& params, const char* key, double dflt);
int    readInt(const nlohmann::json& params, const char* key, int dflt);
bool   readBool(const nlohmann::json& params, const char* key, bool dflt);
String readString(const nlohmann::json& params, const char* key,
                  const String& dflt = "");

/**
 * @brief 一组"一起定仓的腿"
 *
 * 配对交易的两条腿是**同一组**（pair_id 相同）—— 它们的仓位必须一起决定：
 * 组内按 strength（= 1 : β）拆分，组间由 sizing 模型决定各自的**总尺度**。
 * 普通标的各自是单腿组（pair_id = 0）。
 */
struct PortfolioGroup {
    uint64_t          _id = 0;        // pair_id（0 = 非配对单腿组）
    String            _key;           // 统计键：配对组 = "pair.<id>"，单腿组 = 标的代码
    Vector<symbol_t>  _legs;          // 组内建仓腿
    Map<symbol_t, double> _strengths; ///< 每腿原始 strength（配对：A=1、B=β）
    Map<symbol_t, int>    _dirs;      ///< 每腿方向：+1 做多、−1 做空
    double             _strength_sum = 0.0;

    /// 组内相对份额（strength 归一化，Σ = 1）
    double shareOf(symbol_t symbol) const {
        if (_strength_sum <= 0.0) {
            return _legs.size() > 0 ? 1.0 / static_cast<double>(_legs.size()) : 0.0;
        }
        const auto it = _strengths.find(symbol);
        return (it != _strengths.end()) ? it->second / _strength_sum : 0.0;
    }
};

/**
 * @brief 组级交易统计（凯利的输入）
 *
 * win_rate = 盈利笔数 / 总笔数；avg_win / avg_loss = 平均盈利 / 平均亏损幅度。
 * 统计的**单位是组**（配对组就是"一次配对交易"），不是单条腿 —— 配对的两条腿
 * 方向相反，各自的胜率天然 ≈ 50%，拿腿级胜率算凯利没有任何信息量。
 */
struct GroupStats {
    double _win_rate = 0.0;
    double _avg_win = 0.0;
    double _avg_loss = 0.0;
    int    _samples = 0;      ///< 已平仓笔数 —— 样本太少时凯利估计不可信
    bool   _valid = false;
};

/**
 * @brief 组合计算上下文
 *
 * 只包含模型真正需要的东西 —— 不把整个 PortfolioNode 暴露给模型。
 */
struct PortfolioContext {
    DataContext*                      _data = nullptr;
    const Set<symbol_t>*              _symbol_pool = nullptr;  // 节点交易池（因子剥离用）
    Vector<symbol_t>                  _buy_symbols;      // 本轮有 BUY 决策的标的
    /// 本轮**要建仓**的标的：BUY 新开多 + SELL 新开空。
    ///
    /// 与 `_buy_symbols` 的差别（配对场景下它决定两腿的名义比）：
    ///   `_buy_symbols` 只收 BUY 腿 → 配对的空头腿（SELL）压根不在权重表里，
    ///   拿不到 {sym}.strength，只能退回 PortfolioNode 的兜底公式
    ///   `targetCapital / decisions.size()` → 腿名义比恒为 1:2 而不是 1:β，
    ///   PairNode 发布的 {B}.strength = β 形同虚设。
    ///
    /// 本集合只收**真正占用资金**的腿：有持仓的是平仓腿、"已有多仓不再加仓"
    /// 的 BUY 腿都不在内 —— 混进归一化分母会把开仓腿的权重稀释（Σw < 1）。
    Vector<symbol_t>                  _open_symbols;
    /// 本轮要建仓的腿按 pair_id 分成的组。组内拆分依据 strength（= 1 : β）。
    Vector<PortfolioGroup>            _groups;
    double                            _target_capital = 0.0;
    std::function<double(symbol_t)>   _get_quote_price;
    std::function<int64_t(symbol_t)>  _get_position_qty;
    /// 告警回调（由节点按 live / backtest 分流到不同日志）
    std::function<void(const String&)> _warn;

    void warn(const std::string& msg) const {
        if (_warn) _warn(String(msg));
    }
};

/// 组级统计的 context key：配对组用 "pair.<pair_id>"，单腿组用标的代码。
/// 单腿组沿用 "{symbol}.win_rate" —— 与 KellyModel 改造前的键名一致。
inline String groupStatsKey(uint64_t group_id, symbol_t symbol) {
    if (group_id == 0) {
        return get_symbol(symbol);
    }
    return String("pair.") + std::to_string(group_id);
}

/// 把"组内总权重"展开成各腿权重：weight_leg = group_weights[i] × share_leg
/// （group_weights 与 groups 同序、同长度）
Map<symbol_t, double> expandGroupWeights(
    const Vector<PortfolioGroup>& groups,
    const Vector<double>& group_weights);

/// 读组级统计：{key}.win_rate / {key}.avg_win / {key}.avg_loss
GroupStats readGroupStats(DataContext& context, const String& key);

/**
 * @brief 组合计算模型基类
 *
 * 约定：`compute()` 返回 symbol → 占 targetCapital 的比例。
 * 返回空 map 表示"不调整"（调用方按等权处理）。
 */
class PortfolioModel {
public:
    virtual ~PortfolioModel() = default;

    /// 解析公共参数（单标的 / 总仓位上限）后交给子类解析自己的参数
    bool Init(const nlohmann::json& config);

    /// 每次运行开始时的状态重置（无状态模型不用管）
    virtual void Prepare() {}

    /// 计算权重
    virtual Map<symbol_t, double> compute(PortfolioContext& ctx) = 0;

    /// 诊断名（NODE_IO_LOG 用）
    virtual const char* name() const = 0;

    /// 本模型对应的方法枚举
    SizingMethod method() const { return _method; }

protected:
    /// 子类解析自己的参数
    virtual bool onInit(const nlohmann::json& /*config*/) { return true; }

    /// 单标的上限 + 总仓位上限截断
    ///
    /// MVO 不走这里 —— 它的 QP 已经带了 0 ≤ w ≤ w_max 与 Σw = e，
    /// 再做一次比例缩放会破坏等式约束。
    Map<symbol_t, double> applyCaps(Map<symbol_t, double> weights) const;

    SizingMethod _method = SizingMethod::Equal;
    double _max_single_pct = 1.0;   // 单标的上限（默认不限制）
    double _max_total_pct = 1.0;    // 总仓位上限（默认不限制）
};

/// 工厂：按方法创建对应的模型（只创建被选中的那一个）
std::unique_ptr<PortfolioModel> createPortfolioModel(SizingMethod method);

}  // namespace portfolio
