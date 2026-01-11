#include "Nodes/ExecuteNode.h"
#include "Bridge/SIM/SIMExchange.h"
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
    auto simExchagne = dynamic_cast<StockSimulation*>(exchange);
    if (simExchagne) {
        simExchagne->SetSlippage(slippage);
    }
    return true;
}

bool ExecuteNode::Process(const String& strategy, DataContext& context) {
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
