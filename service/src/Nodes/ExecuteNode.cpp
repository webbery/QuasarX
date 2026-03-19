#include "Nodes/ExecuteNode.h"
#include "Nodes/ExecutionPlan.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "MarketTiming/ImmediateTiming.h"
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

bool ExecuteNode::Process(const String& strategy, DataContext& context) {
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

            //INFO("ExecuteNode: {} {} {} shares @ {}",
            //    item._action, get_symbol(item._symbol), item._quantity, item._limitPrice);
        }

        // 执行完毕后清理执行计划
        context.erase("execution_plan");
    }

    context.ConsumeSignals();
    return true;
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
