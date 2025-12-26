#include "MarketTiming/ImmediateTiming.h"
#include "DataContext.h"
#include "server.h"
#include "BrokerSubSystem.h"

bool ImmediateTiming::processSignal(const String& strategy, const TradeSignal& signal, const DataContext& context)
{
    auto broker = _server->GetBrokerSubSystem();
    auto symbol = signal.GetSymbol();
    Order order;
    switch (signal.Action()) {
    case TradeAction::BUY:
        broker->Buy(strategy, symbol, order, [symbol, this](const TradeReport& report) {
            auto sock = Server::GetSocket();
            auto info = to_sse_string(symbol, report);
            nng_send(sock, info.data(), info.size(), 0);
            _reports.emplace_back(std::make_pair(symbol, report));
            });
    break;
    case TradeAction::SELL:
        broker->Sell(strategy, symbol, order, [symbol, this](const TradeReport& report) {
            auto sock = Server::GetSocket();
            auto info = to_sse_string(symbol, report);
            nng_send(sock, info.data(), info.size(), 0);
            _reports.emplace_back(std::make_pair(symbol, report));
            });
    break;
    case TradeAction::EXEC:
    break;
    default:
    break;
    }
    return true;
}

