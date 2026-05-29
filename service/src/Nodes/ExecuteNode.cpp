#include "Nodes/ExecuteNode.h"
#include "Nodes/ExecutionPlan.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Bridge/SlippageModel.h"
#include "MarketTiming/ImmediateTiming.h"
#include "MarketTiming/ShadowTiming.h"
#include "server.h"
#include "RiskContext.h"

namespace{
    class BasicSignalObserver : public ISignalObserver {
    public:
        BasicSignalObserver(Server* server):_server(server) {}
        ~BasicSignalObserver() {
            delete _strategy;
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

bool ExecuteNode::Init(const nlohmann::json& config) {
    int type = config["params"]["type"]["value"];
    auto& slippageConfig = config["params"]["slippageModel"];
    int modelType = slippageConfig["value"].get<int>();

    // 构建滑点模型 JSON 配置
    nlohmann::json slipJson;
    slipJson["type"] = modelType;

    if (modelType == 0) {
        // 固定比例：使用 slippage.value，默认值为 0
        double slippageValue = 0.0;
        if (config["params"].contains("slippage") &&
            config["params"]["slippage"].contains("value")) {
            slippageValue = config["params"]["slippage"]["value"].get<double>();
        }
        slipJson["ratio"] = slippageValue;
    } else {
        // 成交量冲击：使用 base / impact_k / alpha
        slipJson["base"] = config["params"]["slippageBase"]["value"].get<double>();
        slipJson["impact_k"] = config["params"]["slippageImpactK"]["value"].get<double>();
        slipJson["alpha"] = config["params"]["slippageAlpha"]["value"].get<double>();
    }

    _timing = GenerateTiming((ExecuteType)type);

    auto exchange = _server->GetExchangeManager()->GetExchangeByType(ExchangeType::EX_HX);
    auto simExchange = dynamic_cast<HistorySimulationBase*>(exchange);
    if (simExchange) {
        simExchange->SetSlippageModel(SlippageFactory::create(slipJson));
    }
    return true;
}

NodeProcessResult ExecuteNode::Process(const String& strategy, DataContext& context) {
    // ── 风控短路处理：RiskContext triggered 时执行紧急平仓 ──
    auto* rc = context.GetRiskContext();
    if (rc && rc->triggered && rc->action == RiskAction::Close) {
        INFO("[ExecuteNode] Emergency close triggered: type={}", to_string(rc->trigger_type));

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
                            INFO("[ExecuteNode] Emergency sell: {} qty={}", get_symbol(symbol), qty);
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
                    INFO("[ExecuteNode] Emergency sell: {} qty={}", get_symbol(pos._symbol), pos._holds);
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

            INFO("ExecuteNode: {} {} {} shares @ {}",
                (int)item._action - 1, get_symbol(item._symbol), item._quantity, item._limitPrice);
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
    // 检查是否为影子模式
    if (_server->GetRunningMode() == RuningType::Shadow) {
        // 影子模式：创建基础 Timing 策略并包装为 ShadowTiming
        ShadowConfig config;
        config.slippageRate = 0.001;  // 默认滑点 0.1%
        config.initialCapital = BACKTEST_INITIAL_CAPITAL;  // 默认初始资金 50 万

        // 创建基础策略（用于获取信号类型）
        ITimingStrategy* baseTiming = nullptr;
        switch (type)
        {
        case ExecuteType::ImmediatlyLimit:
            baseTiming = new ImmediateTiming(_server, true);
            break;
        case ExecuteType::ImmediatlyMarket:
            baseTiming = new ImmediateTiming(_server, false);
            break;
        default:
            baseTiming = new ImmediateTiming(_server, false);
            break;
        }

        // 包装为影子模式（不使用 baseTiming，直接由 ShadowTiming 处理）
        delete baseTiming;  // 不需要包装，ShadowTiming 自己处理
        return new ShadowTiming(_server, config);
    }

    // 非影子模式：使用原有逻辑
    switch (type)
    {
    case ExecuteType::ImmediatlyLimit:
        return new ImmediateTiming(_server, true);
    case ExecuteType::ImmediatlyMarket:
        return new ImmediateTiming(_server, false);
    default:
        break;
    }
    return nullptr;
}

const List<Pair<symbol_t, TradeReport>>& ExecuteNode::GetReports() const {
    return _timing->GetReports();
}
