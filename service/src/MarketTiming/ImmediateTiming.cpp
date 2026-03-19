#include "MarketTiming/ImmediateTiming.h"
#include "DataContext.h"
#include "server.h"
#include "BrokerSubSystem.h"

bool ImmediateTiming::processSignal(const String& strategy, const TradeSignal& signal, const DataContext& context)
{
    auto broker = _server->GetBrokerSubSystem();
    auto symbol = signal.GetSymbol();
    // TODO: 初始化订单
    Order order;
    order._order[0]._price = signal.Price();
    order._volume = signal.Quantity();
    switch (signal.Action()) {
    case TradeAction::BUY:
        broker->Buy(strategy, symbol, order, [symbol, this](const TradeReport& report) {
            auto sock = Server::GetSocket();
            auto info = to_sse_string(symbol, report);
            nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
            _reports.emplace_back(std::make_pair(symbol, report));
            });
    break;
    case TradeAction::SELL:
        broker->Sell(strategy, symbol, order, [symbol, this](const TradeReport& report) {
            auto sock = Server::GetSocket();
            auto info = to_sse_string(symbol, report);
            nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
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

