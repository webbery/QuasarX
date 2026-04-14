#include "Nodes/ExecuteNode.h"
#include "Nodes/ExecutionPlan.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "MarketTiming/ImmediateTiming.h"
#include "MarketTiming/ShadowTiming.h"
#include "server.h"

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
    double slippage = config["params"]["slippage"]["value"];

    _timing = GenerateTiming((ExecuteType)type);
    
    auto exchange = _server->GetAvaliableStockExchange();
    auto simExchagne = dynamic_cast<StockHistorySimulation*>(exchange);
    if (simExchagne) {
        simExchagne->SetSlippage(slippage);
    }
    return true;
}

NodeProcessResult ExecuteNode::Process(const String& strategy, DataContext& context) {
    // 从 PortfolioNode 读取执行计划
    if (context.GetExecutionPlan()._hasChanged) {
        const auto& plan = context.GetExecutionPlan();

        for (const auto& item : plan._items) {
            if (item._action == TradeAction::HOLD) {
                continue;  // 保持仓位，不下单
            }

            auto* signal = new TradeSignal(item._symbol, item._action);
            signal->SetQuantity(item._quantity);
            signal->SetPrice(item._limitPrice);
            context.AddSignal(signal);

            INFO("ExecuteNode: {} {} {} shares @ {}",
                (int)item._action - 1, get_symbol(item._symbol), item._quantity, item._limitPrice);
        }

        // 执行完毕后清理执行计划
        context.erase("execution_plan");
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
