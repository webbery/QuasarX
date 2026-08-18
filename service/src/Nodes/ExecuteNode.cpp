#include "Nodes/ExecuteNode.h"
#include "ExchangeManager.h"
#include "Nodes/ExecutionPlan.h"
#include "MarketTiming/ImmediateTiming.h"
#include "MarketTiming/ManualTiming.h"
#include "MarketTiming/ShadowTiming.h"
#include "server.h"
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
    // ── 风控短路处理：RiskContext triggered 时执行紧急平仓 ──
    auto* rc = context.GetRiskContext();
    if (rc && rc->triggered && rc->action == RiskAction::Close) {
        if (_server->GetRunningMode() != RuningType::Backtest) {
            STRATEGY_INFO(strategy, "[ExecuteNode] Emergency close triggered: type={}", to_string(rc->trigger_type));
        } else {
            INFO("[ExecuteNode] Emergency close triggered: type={}", to_string(rc->trigger_type));
        }

        // 获取当前所有持仓并生成平仓信号
        if (_server->GetRunningMode() == RuningType::Backtest) {
            auto* histExchange = dynamic_cast<HistorySimulationBase*>(
                _server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
            if (histExchange) {
                auto run_id = context.getBacktestRunId();
                auto btCtx = histExchange->getBacktestContext(run_id);
                if (btCtx) {
                    for (const auto& symbol : btCtx->getSymbols()) {
                        int64_t qty = btCtx->getPosition(symbol);
                        if (qty > 0) {
                            auto* signal = new TradeSignal(symbol, TradeAction::SELL);
                            signal->SetQuantity(qty);
                            signal->SetFlag(1); // 平仓
                            if (context.Current() > 0) {
                                signal->SetBacktestTime(context.Current());
                            }
                            context.AddSignal(signal);
                            if (_server->GetRunningMode() != RuningType::Backtest) {
                                STRATEGY_INFO(strategy, "[ExecuteNode] Emergency sell: {} qty={}", get_symbol(symbol), qty);
                            } else {
                                INFO("[ExecuteNode] Emergency sell: {} qty={}", get_symbol(symbol), qty);
                            }
                        }
                    }
                }
            }
        } else {
            auto& ap = _server->GetPosition("");
            for (const auto& pos : ap._positions) {
                if (pos._holds > 0) {
                    auto* signal = new TradeSignal(pos._symbol, TradeAction::SELL);
                    signal->SetQuantity(pos._holds);
                    signal->SetFlag(1); // 平仓
                    context.AddSignal(signal);
                    STRATEGY_INFO(strategy, "[ExecuteNode] Emergency sell: {} qty={}", get_symbol(pos._symbol), pos._holds);
                }
            }
        }

        context.ConsumeSignals();
        rc->reset();
        return NodeProcessResult::Success;
    }

    // 从 PortfolioNode 读取执行计划
    if (context.GetExecutionPlan()._hasChanged) {
        const auto& plan = context.GetExecutionPlan();

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
                // 该 symbol 不在 plan 中，说明被 PortfolioNode 跳过了
                if (signal->GetAction() == TradeAction::BUY || signal->GetAction() == TradeAction::SELL) {
                    // 数量不足或其他原因被跳过，标记为 HOLD 防止 ImmediateTiming 执行
                    signal->SetAction(TradeAction::HOLD);
                }
            }
        }

        for (const auto& item : plan._items) {
            auto* signal = context.getSignalBySymbol(item._symbol);
            if (item._action == TradeAction::HOLD) {
                if (signal) {
                    signal->SetAction(TradeAction::HOLD);
                }
                continue;  // 保持仓位，不下单
            }

            if (signal) {
                signal->SetAction(item._action);
            }
            else {
                signal = new TradeSignal(item._symbol, item._action);
                context.AddSignal(signal);
            }
            signal->SetQuantity(item._quantity);
            signal->SetPrice(item._limitPrice);

            // 设置回测时间 (从 DataContext 获取当前 Bar 时间)
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
        // Plan 没有变化，但可能存在旧的 signal（quantity=0, price=0）
        // 将所有 BUY/SELL signal 改为 HOLD，防止被 ConsumeSignals 消费
        const auto& allSignals = context.getAllSignals();
        for (const auto& [sym, signal] : allSignals) {
            if (signal->GetAction() == TradeAction::BUY || signal->GetAction() == TradeAction::SELL) {
                if (signal->GetQuantity() <= 0) {
                    signal->SetAction(TradeAction::HOLD);
                }
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
