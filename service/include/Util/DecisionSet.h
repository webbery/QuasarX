#pragma once
#include "std_header.h"
#include "json.hpp"
#include "Decision.h"
#include "DataContext.h"
#include "Nodes/ProtectionNode.h"
#include "Util/datetime.h"

/**
 * @brief 一组决策的唯一权威产物
 *
 * 一组决策 = 一个 DecisionSet。调用方决定它对应什么范围：一次日终回放、
 * 某个 bar 的快照、一段回测 —— 这里只规定"这组决策是什么"，
 * 不规定它由哪条管线产生。
 *
 * 存在的意义：把产出决策的地方（ExecuteNode / ManualTiming / ProtectionNode /
 * 资金账本）与消费决策的地方分开。JSON 日报、邮件、SSE、DuckDB decisions、
 * DuckDB daily_positions、EodReport 全部只读这一个对象，各自不再去摸
 * ManualTiming / ProtectionNode / CapitalPool / PortfolioSubSystem 的内部状态。
 *
 * 统一了此前散在各处的语义：
 *  - 动作只有一套规范表示（TradeAction + flag，与 ExecuteNode 产出的 TradeSignal
 *    同形，无损）；DuckDB 的 2bit DecisionAction 由 decisionAction() 派生，
 *    不再是第二份真相，也不再由各处手写字符串；
 *  - HOLD 与 HOLD 来源（策略评估 vs 默认补）都留在集合里，落盘的 hold_count
 *    不再恒为 0；
 *  - status 词汇集中定义，消费侧不再各自 rank 而恒为 "completed"；
 *  - 归属时间（_asOf）只在这里定一次，各下游不再各自取 now()/time(nullptr)。
 */

/**
 * @brief 单个标的的决策快照
 *
 * 由 ManualTiming 累积（per-symbol 覆盖，同 symbol 只留最终决策），
 * 也被 EodReport 与 BuildDecisionSet 读取。
 */
struct DecisionSnapshot {
    symbol_t    _symbol;
    TradeAction _action = TradeAction::HOLD;
    int64_t     _quantity = 0;
    double      _price = 0.0;
    unsigned char _flag = 0;     // 0=开仓, 1=平仓（仅 BUY/SELL 有意义）
    int         _epoch = -1;
    bool        _signalEvaluated = false;  // true=策略评估产生的；false=默认补 HOLD
};

/**
 * @brief 供持久化与推送使用的紧凑动作码（2bit，与 DuckDB decisions.action 一致）
 *
 * 仅在非 HOLD 时有意义 —— HOLD 不落 decisions 表，也不进 SSE。
 */
inline bool toDecisionCode(TradeAction action, unsigned char flag, DecisionAction& out) {
    switch (action) {
        case TradeAction::BUY:
            out = flag ? DecisionAction::CloseShort : DecisionAction::OpenLong;
            return true;
        case TradeAction::SELL:
            out = flag ? DecisionAction::CloseLong : DecisionAction::OpenShort;
            return true;
        default:
            return false;   // HOLD / EXEC 没有对应的持久化动作码
    }
}

/**
 * @brief 决策状态
 *
 * 词汇与排序的唯一定义处。此前日报的 rank() 只认 "failed"/"partial"，
 * 而生产方只发 "error"/"completed"，于是 "error" 落到兜底分支得 0 分，
 * 顶层 status 永远停在 "completed" —— 策略报错在日报上看不出来。
 * 聚合多个来源时请用 decision_status_rank()，不要再写一份。
 */
enum class DecisionStatus : uint8_t {
    Completed = 0,
    Partial,
    Error,
};

inline const char* to_string(DecisionStatus s) {
    switch (s) {
        case DecisionStatus::Partial:   return "partial";
        case DecisionStatus::Error:     return "error";
        case DecisionStatus::Completed: break;
    }
    return "completed";
}

inline DecisionStatus parse_decision_status(const String& s) {
    if (s == "error")   return DecisionStatus::Error;
    if (s == "partial") return DecisionStatus::Partial;
    return DecisionStatus::Completed;
}

// 严重度：Error > Partial > Completed。聚合时取最差的一个。
inline int decision_status_rank(DecisionStatus s) {
    switch (s) {
        case DecisionStatus::Error:     return 2;
        case DecisionStatus::Partial:   return 1;
        case DecisionStatus::Completed: return 0;
    }
    return 0;
}

/**
 * @brief 一组决策
 */
struct DecisionSet {
    // ── 归属（下游唯一口径，禁止各自再取系统时钟）──
    String _strategy;
    String _asOf;          // 这组决策归属的时刻/交易日（调用方给定，如 "2026-10-22"）
    String _runId;         // 生成批次标识，便于把各下游产物对回同一批
    String _generatedAt;   // wall clock，生成时间（仅供人读，不参与业务判定）
    DecisionStatus _status = DecisionStatus::Completed;
    String _error;

    // ── 预算快照（唯一资金口径）──
    double _allocated = 0.0;   // 分配总额
    double _available = 0.0;   // 可用余额
    double _equity = 0.0;      // 权益 = 可用 + 持仓市值
    bool   _budgetKnown = false;  // 账本缺失时为 false，读侧据此降级而非显示 0

    // ── 决策 ──
    struct Item {
        symbol_t       _symbol;
        TradeAction    _action = TradeAction::HOLD;  // 规范表示
        unsigned char  _flag = 0;                    // 0=开仓 1=平仓
        int64_t        _quantity = 0;
        double         _price = 0.0;      // 决策价
        double         _closePrice = 0.0; // 当期真实收盘价（持仓估值用这个）
        int32_t        _epoch = -1;
        bool           _isDefaultHold = false;  // true = 非策略评估产生的默认 HOLD

        bool isHold() const { return _action == TradeAction::HOLD; }

        // 派生：落盘/推送用的 2bit 动作码；HOLD 返回 false
        bool decisionAction(DecisionAction& out) const {
            return toDecisionCode(_action, _flag, out);
        }
    };
    Vector<Item> _items;   // 含 HOLD，按 symbol 唯一

    // ── 随这组决策一并产出的风控事件 ──
    Vector<ProtectionNode::ProtectionEvent> _riskEvents;

    // ── 组合估值（持仓 × 收盘价）──
    double _portfolioValue = 0.0;

    bool empty() const { return _items.empty(); }

    // JSON 往返：日终回调跨线程传的是 nlohmann::json，DecisionSet 走这条通道。
    // 风险事件只序列化展示字段（供人工复盘），不参与任何计算。
    nlohmann::json toJson() const;
    static DecisionSet fromJson(const nlohmann::json& j);

    // 按动作分组（读侧统一走这里，避免各自重写分类逻辑）
    Vector<const Item*> ofAction(TradeAction a) const {
        Vector<const Item*> out;
        for (const auto& it : _items) {
            if (it._action == a) out.push_back(&it);
        }
        return out;
    }
    const Item* find(symbol_t sym) const {
        for (const auto& it : _items) {
            if (it._symbol == sym) return &it;
        }
        return nullptr;
    }
    bool isHold(symbol_t sym) const {
        const Item* it = find(sym);
        return it && it->isHold();
    }
};

// 失败路径的构造器：没有决策、没有预算，只带身份与错误原因。
// 失败也是一次运行，产物同样有归属日和状态，下游不需要为它单开一套分支。
struct DecisionSetBuilder {
    static DecisionSet Error(const String& strategy, const String& asOf, const String& error) {
        DecisionSet set;
        set._strategy = strategy;
        set._asOf = asOf;
        set._status = DecisionStatus::Error;
        set._error = error;
        set._generatedAt = ToString(Now(), "%Y-%m-%d %H:%M:%S");
        return set;
    }

    // 回测路径的构造器：没有 StrategyCapital 账本，预算字段全部置 0 并标记 _budgetKnown=false。
    // 邮件据此降级（不显示资金行、不算占比），而不是显示 ¥0 让读者以为"钱花光了"。
    static void BuildForBacktest(
        const String& strategy,
        const String& asOf,
        const Map<symbol_t, DecisionSnapshot>& decisions,
        Vector<ProtectionNode::ProtectionEvent> riskEvents,
        DecisionSet& out)
    {
        out._strategy = strategy;
        out._asOf = asOf;
        out._runId = fmt::format("backtest-{}", asOf);
        out._generatedAt = ToString(Now(), "%Y-%m-%d %H:%M:%S");
        out._status = DecisionStatus::Completed;

        // 预算：回测路径没有 StrategyCapital 账本，全部置 0 并标记不可信
        out._allocated = 0.0;
        out._available = 0.0;
        out._equity = 0.0;
        out._budgetKnown = false;

        // 决策
        out._items.reserve(decisions.size());
        for (const auto& [sym, snap] : decisions) {
            DecisionSet::Item item;
            item._symbol = sym;
            item._action = snap._action;
            item._flag = snap._flag;
            item._quantity = snap._quantity;
            item._price = snap._price;
            item._epoch = snap._epoch;
            item._isDefaultHold = snap._action == TradeAction::HOLD && !snap._signalEvaluated;
            // 回测路径没有真实收盘价，用决策价代替
            item._closePrice = snap._price;
            out._items.push_back(std::move(item));
        }

        out._riskEvents = std::move(riskEvents);
        out._portfolioValue = 0.0;
    }
};

/**
 * @brief 决策集合构建器
 *
 * 把原先散在 ManualTiming / ProtectionNode / 资金账本 / BrokerSubSystem
 * 的状态收拢成单个 DecisionSet。调用方组装好输入后调用一次，
 * 之后所有下游只认返回值。
 *
 * @param strategy        策略名
 * @param asOf            这组决策归属的时刻/交易日（调用方给定）
 * @param runId           生成批次标识
 * @param decisions       per-symbol 决策快照（ManualTiming 累积的 Map）
 * @param riskEvents      随这批决策产出的风控事件（调用方需已按批次裁剪）
 * @param allocated       分配总额
 * @param available       可用余额
 * @param equity          权益
 * @param budgetKnown     预算是否可信（账本缺失时 false，读侧据此降级而非显示 0）
 * @param portfolioValue  组合市值，0 表示不展示
 * @param closePrices     当期真实收盘价（持仓估值的正确口径）
 * @param status          状态
 * @param error           失败原因
 */
DecisionSet BuildDecisionSet(
    const String& strategy,
    const String& asOf,
    const String& runId,
    const Map<symbol_t, DecisionSnapshot>& decisions,
    const Vector<ProtectionNode::ProtectionEvent>& riskEvents,
    double allocated,
    double available,
    double equity,
    bool budgetKnown,
    double portfolioValue,
    const Map<symbol_t, double>& closePrices,
    DecisionStatus status,
    const String& error);