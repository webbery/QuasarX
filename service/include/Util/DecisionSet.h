#pragma once
#include "std_header.h"
#include "json.hpp"
#include "Decision.h"
#include "DataContext.h"
#include "Nodes/ProtectionNode.h"
#include "Util/datetime.h"
#include "Util/TradingCost.h"
#include "Bridge/LedgerStore.h"

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
 * ManualTiming / ProtectionNode 的内部状态。
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
    // 紧急平仓标记（风控触发）。ExecuteNode 产生平仓腿时打的标，
    // 经 signal → 快照 → 决策项一路带下来，是"这是强平不是策略平仓"的唯一来源。
    bool        _isEmergencyClose = false;
    // 平仓成本价（持仓均价，原始价口径）；0 = 未知。
    // 紧急平仓的成本价在产生平仓腿的那一刻从持仓账本取（ExecuteNode），
    // 因为那是唯一知道加权均价的地方；决策链下游不再去摸账本。
    double      _costPrice = 0.0;
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
        double         _costPrice = 0.0;  // 持仓均价（平仓成本价）；0 = 未知/不适用
        int32_t        _epoch = -1;
        bool           _isDefaultHold : 1 = false;  // true = 非策略评估产生的默认 HOLD
        bool           _isEmergencyClose : 1 = false;  // true = 风控触发的紧急平仓
        // 两融标记（用于邮件徽标与两融明细）
        bool           _isShortOpen : 1 = false;    // true = 融券卖出（SELL flag=0）
        bool           _isShortCover : 1 = false;   // true = 买入平空（BUY flag=1）
        bool           _isFinanced : 1 = false;     // true = 融资买入（BUY flag=0 + marginRatio<1）
        double         _marginRatio = 1.0;          // 该笔成交的保证金比例（1.0 = 非两融）
        // 该笔决策的成本拆解（佣金/印花税/滑点…）。在构建时求值一次，
        // 邮件/日报只读结果，不再各自算一遍费率。
        CostQuote      _cost;

        bool isHold() const { return _action == TradeAction::HOLD; }

        // 派生：落盘/推送用的 2bit 动作码；HOLD 返回 false
        bool decisionAction(DecisionAction& out) const {
            return toDecisionCode(_action, _flag, out);
        }
    };
    Vector<Item> _items;   // 含 HOLD，按 symbol 唯一

    // ── 交易成本（摩擦）──
    // 费率档案 + 求值结果。Stage 0 是策略级单一档案；接入期货/期权后
    // 按 symbol 解析时，这里变成"每个 symbol 一份档案"，求值接口不变。
    CostProfile _costProfile;
    double _totalTradeFriction = 0.0;   // 当日交易摩擦合计（买入+卖出）
    double _totalHoldingCost   = 0.0;   // 当日持有成本合计（日息口径，与交易摩擦分开）

    // ── 两融（信用交易）──
    // 与交易摩擦严格分开：两融是"持仓期按日计提"，交易摩擦是"时点一次付"。
    // 数据来源：账本的 StrategyCreditState（成交回写后持久化，跨进程存活）。
    // 求值：EvaluateHoldingCost(profile, HoldingCostContext) 按日计提融资利息与融券费。
    struct MarginItem {
        symbol_t _symbol;
        int64_t  _borrowedShares = 0;          // 融券在借数量
        double   _shortCostPrice = 0.0;        // 开空均价（空头的成本价）
        double   _borrowedValue = 0.0;         // 融券在借市值（按当期价）
        double   _dailyShortFee = 0.0;         // 当日融券费（按在借市值 × 融券年费率 ÷ 365）
        double   _financedAmount = 0.0;        // 本标的融资本金余额
        double   _dailyFinancingInterest = 0.0;// 当日融资利息（按融资余额 × 融资年利率 ÷ 365）
    };
    Vector<MarginItem> _marginItems;

    bool   _marginKnown = false;         // 两融数据是否可信（账本缺失时 false，读侧降级）
    bool   _hasMargin = false;           // 有无两融敞口（邮件据此决定是否显示区块）
    double _marginRatio = 1.0;           // 初始保证金比例（1.0 = 全额，0.5 = 50%）
    double _maintenanceMarginRatio = 0.3;// 维持保证金比例
    double _financingRate = 0.0;         // 融资年利率
    double _securitiesLendingRate = 0.0; // 融券年费率
    double _warningRatio = 1.5;          // 警戒线（维持担保比例）
    double _liquidationRatio = 1.3;      // 平仓线（维持担保比例）
    double _accrualDays = 1.0;           // 计息天数（日终邮件为 1，跨周末/节假日时按实际天数）
    double _marginEquity = 0.0;          // 保证金总量（口径 B：自有资金 + 证券市值）
    double _usedMargin = 0.0;            // 已占用保证金（口径 B：融资欠款 + 融券市值）
    double _totalShortValue = 0.0;       // 融券在借市值合计
    double _totalShortFee = 0.0;         // 当日融券费合计
    double _totalFinancingInterest = 0.0;// 当日融资利息合计
    double _totalMarginCost = 0.0;       // 当日两融持有成本合计（= _totalShortFee + _totalFinancingInterest）

    // ── 随这组决策一并产出的风控事件 ──
    Vector<ProtectionNode::ProtectionEvent> _riskEvents;

    // ── 持仓快照序列（日终重放期间逐 bar 记录）──
    // daily_positions 的数据源：每个决策 bar 一条，构成连续的持仓/净值序列。
    // 之前只按最终决策写一条，HOLD 日直接断档，绩效接口拿不到 >= 2 个交易日的序列。
    struct PositionSnapshot {
        symbol_t _symbol;
        time_t   _date = 0;        // 交易日（当日 0 点）
        int64_t  _position = 0;    // 当日收盘时的持仓数量
        double   _closePrice = 0;  // 当日收盘价（原始价口径，与成交价同口径）
    };
    Vector<PositionSnapshot> _positionSnapshots;

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

    /**
     * @brief 由决策项构造成本求值上下文
     *
     * 口径：成交金额 = 数量 × 决策价；方向取动作；紧急平仓的标记透传给
     * 费用项（强制平仓的成本放大只在这里生效一次）。
     */
    static CostContext MakeCostContext(const Item& item) {
        CostContext ctx;
        ctx._amount      = static_cast<double>(item._quantity) * item._price;
        ctx._quantity    = item._quantity;
        ctx._isSell      = (item._action == TradeAction::SELL);
        ctx._isEmergency = item._isEmergencyClose;
        return ctx;
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
        DecisionSet& out,
        const Vector<DecisionSet::PositionSnapshot>& positionSnapshots = {},
        const CostProfile& costProfile = CostProfile{},
        const StrategyCreditState& creditState = StrategyCreditState{},
        double marginRatio = 1.0,
        double maintenanceMarginRatio = 0.3,
        double warningRatio = 1.5,
        double liquidationRatio = 1.3,
        double accrualDays = 1.0)
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

        // 费率档案：邮件里的摩擦成本按这份档案算
        out._costProfile = costProfile;

        // 两融配置
        out._marginRatio = marginRatio;
        out._maintenanceMarginRatio = maintenanceMarginRatio;
        out._warningRatio = warningRatio;
        out._liquidationRatio = liquidationRatio;
        out._accrualDays = accrualDays;
        out._financingRate = costProfile.financingRate();
        out._securitiesLendingRate = costProfile.securitiesLendingRate();

        // 检查是否有两融敞口
        out._hasMargin = (creditState.borrowedCash > 0.0 || !creditState.borrowedShares.empty());
        out._marginKnown = (marginRatio > 0.0 && marginRatio <= 1.0);

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
            item._isEmergencyClose = snap._isEmergencyClose;
            // 回测路径没有真实收盘价，用决策价代替
            item._closePrice = snap._price;
            // 持仓成本：回测路径没有 StrategyCapital 账本，普通 SELL 拿不到成本价（留 0，
            // 邮件显示"—"而不是编一个 0）；紧急平仓的成本价由执行计划携带（快照里带过来）。
            item._costPrice = snap._costPrice;

            // 两融标记
            item._marginRatio = marginRatio;
            if (snap._action == TradeAction::SELL && snap._flag == 0) {
                item._isShortOpen = true;
            } else if (snap._action == TradeAction::BUY && snap._flag == 1) {
                item._isShortCover = true;
            } else if (snap._action == TradeAction::BUY && snap._flag == 0 && marginRatio < 1.0) {
                item._isFinanced = true;
            }

            // 摩擦成本：按档案对每笔交易求值一次，邮件只读结果
            if (item._action != TradeAction::HOLD) {
                item._cost = EvaluateTradeCost(costProfile, DecisionSet::MakeCostContext(item));
                out._totalTradeFriction += item._cost.tradeTotal();
            }
            out._items.push_back(std::move(item));
        }

        // 两融明细与日息求值（回测路径也支持）
        if (out._hasMargin && out._marginKnown) {
            for (const auto& [symStr, shares] : creditState.borrowedShares) {
                if (shares <= 0) continue;

                DecisionSet::MarginItem margin;
                margin._symbol = to_symbol(symStr);
                margin._borrowedShares = shares;

                auto avgIt = creditState.shortAvgPrice.find(symStr);
                if (avgIt != creditState.shortAvgPrice.end()) {
                    margin._shortCostPrice = avgIt->second;
                }

                // 回测路径用决策价代替收盘价
                margin._borrowedValue = static_cast<double>(shares) * margin._shortCostPrice;

                HoldingCostContext holdCtx;
                holdCtx._borrowedValue = margin._borrowedValue;
                holdCtx._days = accrualDays;
                CostQuote quote = EvaluateHoldingCost(costProfile, holdCtx);
                margin._dailyShortFee = quote.item(CostKind::SecuritiesLending);

                out._marginItems.push_back(std::move(margin));
                out._totalShortValue += margin._borrowedValue;
                out._totalShortFee += margin._dailyShortFee;
            }

            for (const auto& [symStr, amount] : creditState.financedAmount) {
                if (amount <= 0.0) continue;

                DecisionSet::MarginItem* margin = nullptr;
                for (auto& m : out._marginItems) {
                    if (get_symbol(m._symbol) == symStr) {
                        margin = &m;
                        break;
                    }
                }
                if (!margin) {
                    out._marginItems.emplace_back();
                    margin = &out._marginItems.back();
                    margin->_symbol = to_symbol(symStr);
                }

                margin->_financedAmount = amount;

                HoldingCostContext holdCtx;
                holdCtx._borrowedCash = amount;
                holdCtx._days = accrualDays;
                CostQuote quote = EvaluateHoldingCost(costProfile, holdCtx);
                margin->_dailyFinancingInterest = quote.item(CostKind::Financing);

                out._totalFinancingInterest += margin->_dailyFinancingInterest;
            }

            out._totalMarginCost = out._totalShortFee + out._totalFinancingInterest;
            out._totalHoldingCost = out._totalMarginCost;
        }

        out._riskEvents = std::move(riskEvents);
        out._portfolioValue = 0.0;
        out._positionSnapshots = positionSnapshots;
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
 * @param costPrices      持仓均价（平仓成本价）；缺失的标的成本价记 0，读侧据此降级
 * @param costProfile     费率档案（佣金/印花税/滑点…）；用于对每笔决策求值摩擦成本
 * @param status          状态
 * @param error           失败原因
 * @param creditState     两融敞口（来自账本，成交回写后持久化）
 * @param marginRatio     初始保证金比例（1.0 = 全额，0.5 = 50%）
 * @param maintenanceMarginRatio 维持保证金比例
 * @param warningRatio    警戒线（维持担保比例）
 * @param liquidationRatio 平仓线（维持担保比例）
 * @param accrualDays     计息天数（日终邮件为 1，跨周末/节假日时按实际天数）
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
    const Map<symbol_t, double>& costPrices,
    const CostProfile& costProfile,
    DecisionStatus status,
    const String& error,
    const StrategyCreditState& creditState = StrategyCreditState{},
    double marginRatio = 1.0,
    double maintenanceMarginRatio = 0.3,
    double warningRatio = 1.5,
    double liquidationRatio = 1.3,
    double accrualDays = 1.0);