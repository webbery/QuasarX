#include "Nodes/ExecuteNode.h"
#include "ExchangeManager.h"
#include "Nodes/ExecutionPlan.h"
#include "MarketTiming/ImmediateTiming.h"
#include "MarketTiming/ManualTiming.h"
#include "MarketTiming/ShadowTiming.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "RiskContext.h"
#include "Util/log.h"

namespace{
    class BasicSignalObserver : public ISignalObserver {
    public:
        BasicSignalObserver(Server* server):_server(server) {}
        ~BasicSignalObserver() {
            // _strategy 的生命周期由 ExecuteNode 管理，observer 不负责删除
        }

        virtual void OnSignalConsume(const String& strategy, TradeSignal* signal, const DataContext &context) {
            if (!_strategy)
                return;
            //INFO("signal try consume");
            _strategy->processSignal(strategy, *signal, context);
            
        }

        virtual void RegistTimingStrategy(ITimingStrategy* strategy) {
            _strategy = strategy;
        }
    private:
        Server* _server;
        ITimingStrategy* _strategy = nullptr;
    };
}
ExecuteNode::ExecuteNode(Server* server):_server(server), _timing(nullptr){
}

ExecuteNode::~ExecuteNode() {
    delete _timing;
    _timing = nullptr;
}

bool ExecuteNode::Init(const nlohmann::json& config) {
    ExecuteType type = ExecuteType::Shadow;
    if (config.contains("params") && config["params"].contains("type")
        && config["params"]["type"].contains("value")) {
        int raw = config["params"]["type"]["value"].get<int>();
        int maxVal = static_cast<int>(ExecuteType::Shadow);
        if (raw >= 0 && raw <= maxVal) {
            type = static_cast<ExecuteType>(raw);
        } else {
            WARN("[ExecuteNode] Invalid ExecuteType value: {}, fallback to Shadow", raw);
        }
    }
    _execType = type;

    delete _timing;
    _timing = GenerateTiming(_execType);
    return true;
}

NodeProcessResult ExecuteNode::Process(const String& strategy, DataContext& context) {
    // ── 风控短路处理：RiskContext triggered 时修改 plan（添加平仓腿） ──
    auto* rc = context.GetRiskContext();
    if (rc && rc->triggered && rc->action == RiskAction::Close) {
        if (_server->GetRunningMode() != RuningType::Backtest) {
            STRATEGY_INFO(strategy, "[ExecuteNode] Emergency close triggered: type={}", to_string(rc->trigger_type));

            // 收集被平仓的标的
            Vector<String> closedSymbols;
            auto* broker = _server->GetBrokerSubSystem();
            auto* capital = broker->GetStrategyCapital(strategy);
            for (const auto& [symbol, pos] : capital->getPositions().getAll()) {
                if (pos._quantity > 0) {
                    closedSymbols.push_back(get_symbol(symbol));
                }
            }

            // 根据 ExecuteType 决定邮件发送方式
            ExecuteType execType = context.GetExecuteType();
            if (execType == ExecuteType::Manual) {
                STRATEGY_INFO(strategy, "[ExecuteNode] Emergency close recorded for daily strategy (symbols={})",
                    [&]() {
                        String s;
                        for (const auto& sym : closedSymbols) {
                            if (!s.empty()) s += ",";
                            s += sym;
                        }
                        return s;
                    }());
            } else {
                String symbolsStr;
                for (const auto& sym : closedSymbols) {
                    if (!symbolsStr.empty()) symbolsStr += ", ";
                    symbolsStr += sym;
                }
                String emailContent = std::format(
                    "[风控] {} 触发紧急平仓\n"
                    "触发类型: {}\n"
                    "平仓标的: {}",
                    strategy,
                    int(rc->trigger_type),
                    symbolsStr);
                _server->SendEmail(emailContent);
            }
        } else {
            INFO("[ExecuteNode] Emergency close triggered: type={}", to_string(rc->trigger_type));
        }

        // 修改 plan：为每个持仓添加平仓腿
        auto& plan = context.GetExecutionPlan();
        if (_server->GetRunningMode() != RuningType::Backtest) {
            auto* broker = _server->GetBrokerSubSystem();
            auto* capital = broker->GetStrategyCapital(strategy);
            for (const auto& [symbol, pos] : capital->getPositions().getAll()) {
                if (pos._quantity > 0) {
                    ExecutionItem item;
                    item._symbol = symbol;
                    item._action = TradeAction::SELL;
                    item._quantity = static_cast<int>(pos._quantity);
                    item._flag = 1; // 平仓
                    // 决策价：当前 Bar 原始价（与 PortfolioNode 的 limitPrice 同口径）
                    double price = context.GetRawPrice(symbol);
                    if (price <= 0) {
                        price = pos._avgPrice;
                    }
                    item._limitPrice = price;
                    item._targetValue = item._quantity * price;
                    item._isEmergencyClose = true;
                    // 平仓成本价：持仓账本的加权均价（原始价口径，与决策价同口径）。
                    // 在这里取是因为这是唯一知道均价的地方 —— 邮件要展示平仓的成本价与 P&L，
                    // 决策链下游不再自己摸账本（回测路径没有账本，就只能显示"—"）。
                    item._costPrice = pos._avgPrice;
                    plan._items.push_back(item);
                    STRATEGY_INFO(strategy, "[ExecuteNode] Emergency sell: {} qty={} @ {} (cost={:.2f})",
                                  get_symbol(symbol), pos._quantity, price, pos._avgPrice);
                }
            }
        } else {
            // 回测模式：从 BacktestContext 获取持仓
            auto* histExchange = dynamic_cast<HistorySimulationBase*>(
                _server->GetExchangeManager()->GetExchangeByType(ExchangeType::EX_STOCK_HIST_SIM));
            if (histExchange) {
                auto run_id = context.getBacktestRunId();
                auto btCtx = histExchange->getBacktestContext(run_id);
                if (btCtx) {
                    for (const auto& symbol : btCtx->getSymbols()) {
                        int64_t qty = btCtx->getPosition(symbol);
                        if (qty > 0) {
                            ExecutionItem item;
                            item._symbol = symbol;
                            item._action = TradeAction::SELL;
                            item._quantity = static_cast<int>(qty);
                            item._flag = 1; // 平仓
                            item._limitPrice = 0.0; // 回测模式下价格由 BacktestContext 提供
                            item._targetValue = 0.0;
                            item._isEmergencyClose = true;
                            plan._items.push_back(item);
                            INFO("[ExecuteNode] Emergency sell: {} qty={}", get_symbol(symbol), qty);
                        }
                    }
                }
            }
        }
        plan._hasChanged = true;

        rc->reset();
        // 继续执行下面的 plan → signal 翻译逻辑
    }

    // ── plan → signal 翻译 ──
    const auto& plan = context.GetExecutionPlan();
    if (plan._hasChanged) {
        // 构建 plan 中已处理的 symbol 集合
        Set<symbol_t> plannedSymbols;
        for (const auto& item : plan._items) {
            plannedSymbols.insert(item._symbol);
        }

        // 对于不在 plan 中的 signal（PortfolioNode 评估后跳过，如 quantity < 100），
        // 将其 action 改为 HOLD，避免 ConsumeSignals 时触发无效订单
        const auto& allSignals = context.getAllSignals();
        for (const auto& [sym, signal] : allSignals) {
            if (plannedSymbols.count(sym) == 0) {
                if (signal->GetAction() == TradeAction::BUY || signal->GetAction() == TradeAction::SELL) {
                    signal->SetAction(TradeAction::HOLD);
                }
            }
        }

        // 为 plan 中的每个 item 创建或更新 signal
        for (const auto& item : plan._items) {
            auto* signal = context.getSignalBySymbol(item._symbol);
            if (item._action == TradeAction::HOLD) {
                if (signal) {
                    signal->SetAction(TradeAction::HOLD);
                }
                continue;
            }

            if (signal) {
                signal->SetAction(item._action);
            } else {
                signal = new TradeSignal(item._symbol, item._action);
                context.AddSignal(signal);
            }
            signal->SetQuantity(item._quantity);
            signal->SetPrice(item._limitPrice);
            // 两融标记：flag 决定开/平，是区分"融券卖出"与"卖出平多"的唯一来源。
            // 此前这里没传 flag，ManualTiming 只能硬编码 SELL→flag=1（平多），
            // 导致"融券卖出"（SELL flag=0）在决策集里无法表达，落盘时 OpenShort 永远产生不出来。
            signal->SetFlag(item._flag);
            // 紧急平仓标记与成本价随 signal 传给 ManualTiming。
            // 只在有值时覆盖：同一 symbol 的多条 plan 项里，紧急平仓腿排在最后，
            // 若被后面的普通腿清零，标记和成本价就丢了。
            if (item._isEmergencyClose) {
                signal->SetEmergencyClose(true);
            }
            if (item._costPrice > 0.0) {
                signal->SetCostPrice(item._costPrice);
            }

            if (_server->GetRunningMode() == RuningType::Backtest) {
                signal->SetBacktestTime(context.Current());
            }

            if (_server->GetRunningMode() != RuningType::Backtest) {
                STRATEGY_INFO(strategy, "ExecuteNode: {} {} {} shares @ {}",
                    (int)item._action - 1, get_symbol(item._symbol), item._quantity, item._limitPrice);
            } else {
                INFO("ExecuteNode: {} {} {} shares @ {}",
                    (int)item._action - 1, get_symbol(item._symbol), item._quantity, item._limitPrice);
            }
        }
    } else {
        // Plan 没有变化：这份计划本轮依然有效，把它的量/价/flag 重新套到本轮 signal 上。
        //
        // 不能像以前那样把 BUY/SELL 一律降级为 HOLD：日终逐日回放里，连续两天
        // 算出同一笔同量同价委托（价格相同，或不同价格取整后股数相同）时计划天然
        // 不变，降级会让第二天的决策变成 HOLD —— 而 HOLD 不落库，接口/日报看起来
        // 就像"当天没有决策"。
        //
        // 只有计划里确实没有该标的（PortfolioNode 评估后跳过，如 quantity<100、
        // 已持多仓不再加仓）才降级为 HOLD，保持原来"不消费无效订单"的语义。
        const auto& plan = context.GetExecutionPlan();
        Map<symbol_t, const ExecutionItem*> planned;
        for (const auto& item : plan._items) {
            planned[item._symbol] = &item;
        }

        const auto& allSignals = context.getAllSignals();
        for (const auto& [sym, signal] : allSignals) {
            if (signal->GetAction() != TradeAction::BUY && signal->GetAction() != TradeAction::SELL) {
                continue;
            }

            auto it = planned.find(sym);
            if (it == planned.end()) {
                if (signal->GetQuantity() <= 0) {
                    signal->SetAction(TradeAction::HOLD);
                }
                continue;
            }

            const ExecutionItem& item = *it->second;
            if (item._action == TradeAction::HOLD) {
                signal->SetAction(TradeAction::HOLD);
                continue;
            }
            if (item._quantity > 0) {
                // 与 plan→signal 翻译分支保持同一套字段，决策集与首次下发完全一致
                signal->SetAction(item._action);
                signal->SetQuantity(item._quantity);
                signal->SetPrice(item._limitPrice);
                signal->SetFlag(item._flag);
                if (item._isEmergencyClose) {
                    signal->SetEmergencyClose(true);
                }
                if (item._costPrice > 0.0) {
                    signal->SetCostPrice(item._costPrice);
                }
                if (_server->GetRunningMode() == RuningType::Backtest) {
                    signal->SetBacktestTime(context.Current());
                }
            } else if (signal->GetQuantity() <= 0) {
                signal->SetAction(TradeAction::HOLD);
            }
        }
    }

    // ── DuckDB node_io 日志（仅实盘模式，在 ConsumeSignals 之前）──
    NODE_IO_LOG("execution", _id,
        input["plan_changed"] = context.GetExecutionPlan()._hasChanged;
        nlohmann::json plan_items_json = nlohmann::json::array();
        for (const auto& item : context.GetExecutionPlan()._items) {
            nlohmann::json item_json;
            item_json["symbol"] = get_symbol(item._symbol);
            switch (item._action) {
                case TradeAction::BUY:  item_json["action"] = "BUY"; break;
                case TradeAction::SELL: item_json["action"] = "SELL"; break;
                case TradeAction::HOLD: item_json["action"] = "HOLD"; break;
                default: item_json["action"] = "EXEC"; break;
            }
            item_json["quantity"] = item._quantity;
            item_json["limit_price"] = item._limitPrice;
            item_json["target_value"] = item._targetValue;
            item_json["is_emergency_close"] = item._isEmergencyClose;
            plan_items_json.push_back(item_json);
        }
        input["plan_items"] = plan_items_json;

        const auto& all_signals = context.getAllSignals();
        nlohmann::json signals_consumed_json = nlohmann::json::array();
        nlohmann::json signals_skipped_json = nlohmann::json::array();
        for (const auto& [sym, signal] : all_signals) {
            nlohmann::json sig_json;
            sig_json["symbol"] = get_symbol(sym);
            switch (signal->GetAction()) {
                case TradeAction::BUY:  sig_json["action"] = "BUY"; break;
                case TradeAction::SELL: sig_json["action"] = "SELL"; break;
                case TradeAction::HOLD: sig_json["action"] = "HOLD"; break;
                default: sig_json["action"] = "EXEC"; break;
            }
            sig_json["quantity"] = signal->GetQuantity();
            sig_json["price"] = signal->GetPrice();
            if (signal->GetAction() != TradeAction::HOLD) {
                signals_consumed_json.push_back(sig_json);
            } else {
                signals_skipped_json.push_back(sig_json);
            }
        }
        output["signals_consumed"] = signals_consumed_json;
        output["signals_skipped"] = signals_skipped_json;
    );

    // 为执行计划中尚无 signal 的标的创建 HOLD signal，
    // 确保 ManualTiming 能在日终邮件中展示所有标的的决策（含"保持不变"）
    if (context.GetExecutionPlan()._hasChanged) {
        for (const auto& item : context.GetExecutionPlan()._items) {
            if (!context.getSignalBySymbol(item._symbol)) {
                auto* signal = new TradeSignal(item._symbol, TradeAction::HOLD);
                signal->SetQuantity(item._quantity);
                signal->SetPrice(item._limitPrice);
                if (_server->GetRunningMode() == RuningType::Backtest) {
                    signal->SetBacktestTime(context.Current());
                }
                signal->SetDefaultHold(true);
                context.AddSignal(signal);
            }
        }
    }

    context.ConsumeSignals();
    return NodeProcessResult::Success;
}

void ExecuteNode::Prepare(const String& strategy, DataContext& context)
{
    ISignalObserver* obs = new BasicSignalObserver(_server);
    obs->RegistTimingStrategy(_timing);
    context.RegistSignalObserver(obs);
    
    // 把ExecuteType传递给context，供ProtectionNode等节点使用
    // Why: ProtectionNode需要知道策略类型以决定邮件发送方式
    // （日终策略Manual的紧急平仓记录到context，实时策略立即发邮件）
    context.SetExecuteType(_execType);
}

ITimingStrategy* ExecuteNode::GenerateTiming(ExecuteType type)
{
    switch (type)
    {
    case ExecuteType::Manual:
        return new ManualTiming(_server);
    case ExecuteType::Shadow:
    {
        ShadowConfig config;
        config.slippageRate = 0.001;
        config.initialCapital = BACKTEST_INITIAL_CAPITAL;
        return new ShadowTiming(_server, config);
    }
    case ExecuteType::ImmediatlyLimit:
        return new ImmediateTiming(_server, true);
    case ExecuteType::ImmediatlyMarket:
        return new ImmediateTiming(_server, false);
    default:
        WARN("[ExecuteNode] ExecuteType {} not implemented yet, fallback to Manual",
             static_cast<int>(type));
        return new ManualTiming(_server);
    }
}

const List<Pair<symbol_t, TradeReport>>& ExecuteNode::GetReports() const {
    return _timing->GetReports();
}
