#include "Nodes/ExecuteNode.h"
#include "server.h"

namespace{
    class BasicSignalObserver : public ISignalObserver {
    public:
        BasicSignalObserver(Server* server):_server(server) {}
        virtual void OnSignalConsume(TradeSignal* signal) {
            auto broker = _server->GetBrokerSubSystem();
            INFO("signal try consume");
            /*for (auto& item : decisions) {
                auto& decision = item.second;
                Order order;
                if (decision.action == TradeAction::BUY) {
                    broker->Buy(strategy, decision.symbol, order, [symbol = decision.symbol, this](const TradeReport& report) {
                        auto sock = Server::GetSocket();
                        auto info = to_sse_string(symbol, report);
                        nng_send(sock, info.data(), info.size(), 0);
                        _reports.emplace_back(std::make_pair(symbol, report));
                        });
                }
                else if (decision.action == TradeAction::SELL) {
                    broker->Sell(strategy, decision.symbol, order, [symbol = decision.symbol, this](const TradeReport& report) {
                        auto sock = Server::GetSocket();
                        auto info = to_sse_string(symbol, report);
                        nng_send(sock, info.data(), info.size(), 0);
                        _reports.emplace_back(std::make_pair(symbol, report));
                        });
                }
            }*/
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
    auto timing = GenerateTiming((ExecuteType)type);

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
    case ExecuteType::Immediatly:
        break;
    default:
        break;
    }
    return nullptr;
}
