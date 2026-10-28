#pragma once
#include "std_header.h"
#include <algorithm>
#include <cmath>

/**
 * @brief 交易成本（摩擦）框架
 *
 * 把"一组决策"换算成"按决策价成交要付出的摩擦"，并预留期货/两融的扩展维度。
 *
 * 设计要点（三条，缺一不可）：
 *  1. 计价基准（CostBasis）是主键：费用项声明自己作用在"哪个量"上。
 *     成交金额（股票佣金/印花税）、数量（期货按手、期权按张）、
 *     借入现金余额（融资利息）、借券市值（融券费）——新增资产类别只是
 *     新增一条 FeeItem，而不是新增一套平行模型。
 *  2. 时间维度（CostTiming）必须分离：交易时点一次付的费用（佣金/印花税/滑点）
 *     与持仓期间按日累计的费用（融资利息/融券费）口径不同、量纲不同，
 *     混成一个"摩擦"数会误导读者。求值与展示都分两块。
 *  3. 保证金占用不是费用：_marginRatio 只描述资金占用，不进摩擦合计。
 *
 * 费率来源（CostProfile）应按 symbol 解析，而不是"整个策略一个费率"：
 *   实盘 → ExchangeInterface::GetCommission(symbol)（券商费率表）
 *   模拟 → 各 Exchange 的 GetDefaultCommission()
 *   策略 JSON 参数 → 显式覆盖/假设，需标注
 */

namespace trading_cost {
// 策略未显式配置时的缺省值（与券商默认佣金、RecordManualFill 默认滑点一致）
constexpr double kDefaultCommissionRate = 9e-05;   // 佣金 万0.9
constexpr double kDefaultStampDutyRate  = 0.001;    // 卖出印花税 0.1%
constexpr double kDefaultMinFee         = 5.0;      // 单笔最低佣金（元）
constexpr double kDefaultSlippageRate   = 0.0005;   // 滑点 5bp
constexpr double kDaysPerYear           = 365.0;    // 计息天数基准（同 MarginAccount）
}  // namespace trading_cost

/**
 * @brief 计价基准：费用作用在哪个量上
 */
enum class CostBasis {
    Notional,       // 成交金额（股票/ETF 佣金、印花税）
    Quantity,       // 数量/手数（期货按手、期权按张）
    ContractFace,   // 合约面值（期权）
    BorrowedCash,   // 借入现金余额（融资利息）
    BorrowedValue,  // 借券市值（融券费）
    HoldingNotional,// 持仓名义额（期货持仓费/交割）
};

/**
 * @brief 时间维度：一次性收取 vs 按日累计
 */
enum class CostTiming {
    PerTrade,  // 交易时点一次收取
    PerDay,    // 持仓期间按日累计
};

/**
 * @brief 费用种类
 */
enum class CostKind {
    Commission,         // 佣金
    StampDuty,          // 印花税
    ExchangeFee,        // 交易所/结算费
    Slippage,           // 滑点
    Financing,          // 融资利息
    SecuritiesLending,  // 融券费
};

inline const char* to_string(CostKind k) {
    switch (k) {
        case CostKind::Commission:        return "commission";
        case CostKind::StampDuty:         return "stamp_duty";
        case CostKind::ExchangeFee:       return "exchange_fee";
        case CostKind::Slippage:          return "slippage";
        case CostKind::Financing:         return "financing";
        case CostKind::SecuritiesLending: return "securities_lending";
    }
    return "unknown";
}

inline CostKind cost_kind_from_string(const String& s) {
    if (s == "commission")        return CostKind::Commission;
    if (s == "stamp_duty")        return CostKind::StampDuty;
    if (s == "exchange_fee")      return CostKind::ExchangeFee;
    if (s == "slippage")          return CostKind::Slippage;
    if (s == "financing")         return CostKind::Financing;
    if (s == "securities_lending") return CostKind::SecuritiesLending;
    return CostKind::Commission;
}

/**
 * @brief 费用项：一条费率规则
 */
struct FeeItem {
    CostKind  _kind  = CostKind::Commission;
    CostBasis _basis = CostBasis::Notional;
    CostTiming _timing = CostTiming::PerTrade;
    // 比率（Notional/Borrowed*/HoldingNotional 口径）；
    // Quantity 口径下表示"每单位金额"（期货每手 X 元、期权每张 Y 元）
    double    _rate  = 0.0;
    double    _min   = 0.0;   // 最低收费（0 = 无下限）
    double    _max   = 0.0;   // 上限（0 = 不限）
    bool      _sellOnly = false;  // 仅卖出收取
    bool      _buyOnly  = false;  // 仅买入收取
    // 紧急平仓倍率：强制平仓时成本被放大（滑点惩罚等），1.0 = 不放大
    double    _emergencyMultiplier = 1.0;
};

/**
 * @brief 费率档案：一类标的的完整成本规则
 *
 * Stage 0 为策略级单一档案；接入期货/期权后按 symbol 解析时，
 * 这里仍是同一份结构，求值接口不变。
 */
struct CostProfile {
    Vector<FeeItem> _tradeFees;    // 交易时点费用
    Vector<FeeItem> _holdingFees;  // 持仓期间费用（日频）
    double _marginRatio = 1.0;     // 保证金比例（资金占用，不是费用）
    bool   _known = false;         // 费率是否已配置（false → 读侧降级，不显示 0）

    /**
     * @brief 费率是否已配置
     * @return false 表示这份档案没有费率（读侧应显示"未配置"而不是报 0）
     */
    bool known() const { return _known; }

    /**
     * @brief 股票口径档案（Stage 0）
     * @param commissionRate 佣金费率（按成交金额）
     * @param stampDutyRate  印花税率（仅卖出收取）
     * @param minFee         单笔最低佣金（元）
     * @param slippageRate   滑点率（按成交金额）
     * @param financingRate  融资年利率（按借入现金余额，按日计提）；0 表示不开融资
     * @param securitiesLendingRate 融券年费率（按借券市值，按日计提）；0 表示不开融券
     */
    static CostProfile ForStock(double commissionRate, double stampDutyRate,
                                double minFee, double slippageRate,
                                double financingRate = 0.0,
                                double securitiesLendingRate = 0.0) {
        CostProfile p;
        p._known = true;

        FeeItem commission;
        commission._kind  = CostKind::Commission;
        commission._basis = CostBasis::Notional;
        commission._rate  = commissionRate;
        commission._min   = minFee;
        p._tradeFees.push_back(commission);

        FeeItem stamp;
        stamp._kind  = CostKind::StampDuty;
        stamp._basis = CostBasis::Notional;
        stamp._rate  = stampDutyRate;
        stamp._sellOnly = true;
        p._tradeFees.push_back(stamp);

        FeeItem slippage;
        slippage._kind  = CostKind::Slippage;
        slippage._basis = CostBasis::Notional;
        slippage._rate  = slippageRate;
        p._tradeFees.push_back(slippage);

        // 持仓期费用：融资利息与融券费。
        // 与交易摩擦严格分开（口径不同、量纲不同，混成一个"摩擦"数会误导读者）：
        //   - 融资利息：base = 借入现金余额，rate = 融资年利率，按日计提
        //   - 融券费：  base = 借券市值，    rate = 融券年费率，按日计提
        // 求值由 EvaluateHoldingCost 完成，base × rate × days / 365。
        if (financingRate > 0.0) {
            FeeItem financing;
            financing._kind  = CostKind::Financing;
            financing._basis = CostBasis::BorrowedCash;
            financing._timing = CostTiming::PerDay;
            financing._rate  = financingRate;
            p._holdingFees.push_back(financing);
        }
        if (securitiesLendingRate > 0.0) {
            FeeItem lending;
            lending._kind  = CostKind::SecuritiesLending;
            lending._basis = CostBasis::BorrowedValue;
            lending._timing = CostTiming::PerDay;
            lending._rate  = securitiesLendingRate;
            p._holdingFees.push_back(lending);
        }

        return p;
    }

    /**
     * @brief 融资年利率（0 = 未配置 / 不开融资）
     */
    double financingRate() const {
        for (const auto& f : _holdingFees) {
            if (f._kind == CostKind::Financing) return f._rate;
        }
        return 0.0;
    }

    /**
     * @brief 融券年费率（0 = 未配置 / 不开融券）
     */
    double securitiesLendingRate() const {
        for (const auto& f : _holdingFees) {
            if (f._kind == CostKind::SecuritiesLending) return f._rate;
        }
        return 0.0;
    }

    /**
     * @brief 是否有持仓期费用（融资利息 / 融券费）
     */
    bool hasHoldingFees() const {
        for (const auto& f : _holdingFees) {
            if (f._rate > 0.0) return true;
        }
        return false;
    }

    /**
     * @brief 取佣金费率（供只需要单一费率的读侧，如交易记录接口）
     * @return 费率；档案未配置或没有佣金项时返回 0
     */
    double commissionRate() const {
        for (const auto& f : _tradeFees) {
            if (f._kind == CostKind::Commission) return f._rate;
        }
        return 0.0;
    }
};

/**
 * @brief 一次交易的成本上下文（求值输入）
 */
struct CostContext {
    double  _amount      = 0.0;   // 成交金额（数量 × 价格）
    int64_t _quantity    = 0;      // 数量（Quantity 口径）
    double  _contractFace = 0.0;   // 合约面值（ContractFace 口径）
    bool    _isSell      = false;  // 方向
    bool    _isEmergency = false;  // 是否紧急平仓
};

/**
 * @brief 持仓期间的成本上下文（日频求值输入）
 */
struct HoldingCostContext {
    double _borrowedCash   = 0.0;  // 借入现金余额
    double _borrowedValue  = 0.0;  // 借券市值
    double _holdingNotional = 0.0; // 持仓名义额
    double _days           = 1.0;  // 计息天数
};

/**
 * @brief 成本求值结果：分项 + 合计
 */
struct CostQuote {
    Map<CostKind, double> _tradeItems;    // 交易时点分项
    Map<CostKind, double> _holdingItems;  // 持仓期间分项

    double item(CostKind kind) const {
        auto it = _tradeItems.find(kind);
        if (it != _tradeItems.end()) return it->second;
        it = _holdingItems.find(kind);
        return it != _holdingItems.end() ? it->second : 0.0;
    }
    double tradeTotal() const {
        double s = 0.0;
        for (const auto& [kind, v] : _tradeItems) s += v;
        return s;
    }
    double holdingTotal() const {
        double s = 0.0;
        for (const auto& [kind, v] : _holdingItems) s += v;
        return s;
    }
    double total() const { return tradeTotal() + holdingTotal(); }
    bool empty() const { return _tradeItems.empty() && _holdingItems.empty(); }
};

/**
 * @brief 按档案求值一次交易的摩擦成本
 *
 * 交易时点费用只算 PerTrade 项；余额型的持仓期费用不在这里出。
 */
inline CostQuote EvaluateTradeCost(const CostProfile& profile, const CostContext& ctx) {
    CostQuote quote;
    if (!profile._known) return quote;

    for (const auto& fee : profile._tradeFees) {
        if (fee._timing != CostTiming::PerTrade) continue;
        if (fee._sellOnly && !ctx._isSell) continue;
        if (fee._buyOnly && ctx._isSell) continue;

        double base = 0.0;
        switch (fee._basis) {
            case CostBasis::Notional:       base = ctx._amount; break;
            case CostBasis::Quantity:       base = static_cast<double>(ctx._quantity); break;
            case CostBasis::ContractFace:   base = ctx._contractFace; break;
            case CostBasis::BorrowedCash:
            case CostBasis::BorrowedValue:
            case CostBasis::HoldingNotional:
                continue;   // 余额/持仓型费用属于持有期，不在交易时点算
        }

        double rate = fee._rate;
        if (ctx._isEmergency) rate *= fee._emergencyMultiplier;

        double amount = base * rate;
        if (fee._min > 0.0) amount = std::max(fee._min, amount);
        if (fee._max > 0.0) amount = std::min(fee._max, amount);
        if (amount == 0.0) continue;

        quote._tradeItems[fee._kind] += amount;
    }
    return quote;
}

/**
 * @brief 按档案求值持仓期间的成本（日息）
 *
 * PerDay 项按 base × rate × days / 365 累计，与 MarginAccount 的日息口径一致。
 */
inline CostQuote EvaluateHoldingCost(const CostProfile& profile, const HoldingCostContext& ctx) {
    CostQuote quote;
    if (!profile._known) return quote;

    for (const auto& fee : profile._holdingFees) {
        if (fee._timing != CostTiming::PerDay) continue;

        double base = 0.0;
        switch (fee._basis) {
            case CostBasis::BorrowedCash:    base = ctx._borrowedCash; break;
            case CostBasis::BorrowedValue:   base = ctx._borrowedValue; break;
            case CostBasis::HoldingNotional: base = ctx._holdingNotional; break;
            case CostBasis::Notional:
            case CostBasis::Quantity:
            case CostBasis::ContractFace:
                continue;   // 成交口径的费用不在这里算
        }

        double amount = base * fee._rate * ctx._days / trading_cost::kDaysPerYear;
        if (fee._min > 0.0) amount = std::max(fee._min, amount);
        if (fee._max > 0.0) amount = std::min(fee._max, amount);
        if (amount == 0.0) continue;

        quote._holdingItems[fee._kind] += amount;
    }
    return quote;
}
