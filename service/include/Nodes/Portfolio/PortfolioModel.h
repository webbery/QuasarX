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
 * @brief 组合计算上下文
 *
 * 只包含模型真正需要的东西 —— 不把整个 PortfolioNode 暴露给模型。
 */
struct PortfolioContext {
    DataContext*                      _data = nullptr;
    const Set<symbol_t>*              _symbol_pool = nullptr;  // 节点交易池（因子剥离用）
    Vector<symbol_t>                  _buy_symbols;      // 本轮有 BUY 决策的标的
    double                            _target_capital = 0.0;
    std::function<double(symbol_t)>   _get_quote_price;
    std::function<int64_t(symbol_t)>  _get_position_qty;
    /// 告警回调（由节点按 live / backtest 分流到不同日志）
    std::function<void(const String&)> _warn;

    void warn(const std::string& msg) const {
        if (_warn) _warn(String(msg));
    }
};

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
