#include "MarketTiming/ImmediateTiming.h"
#include "DataContext.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"

bool ImmediateTiming::processSignal(const String& strategy, const TradeSignal& signal, const DataContext& context)
{
    auto broker = _server->GetBrokerSubSystem();
    auto symbol = signal.GetSymbol();
    // TODO: 初始化订单
    Order order;
    order._price = signal.GetPrice();
    order._volume = signal.GetQuantity();
    auto run_id = context.getBacktestRunId();
    switch (signal.GetAction()) {
    case TradeAction::BUY:
        order._side = 0;
        // 有空仓 → 平空(_flag=1)，无仓 → 开多(_flag=0)
        {
            int64_t pos = 0;
            auto* histExchange = dynamic_cast<StockHistorySimulation*>(
                _server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
            if (histExchange && context.getBacktestRunId()) {
                pos = histExchange->GetPositionQuantity(symbol);
            }
            order._flag = (pos < 0) ? 1 : 0;
        }
        broker->Buy(run_id, strategy, symbol, order, [symbol, this](const TradeReport& report) {
            auto sock = Server::GetSocket();
            auto info = to_sse_string(symbol, report);
            nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
            _reports.emplace_back(std::make_pair(symbol, report));
            });
    break;
    case TradeAction::SELL:
        order._side = 1;
        // 有多仓 → 平多(_flag=1)，无仓 → 做空(_flag=0)
        {
            int64_t pos = 0;
            auto* histExchange = dynamic_cast<StockHistorySimulation*>(
                _server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
            if (histExchange && context.getBacktestRunId()) {
                pos = histExchange->GetPositionQuantity(symbol);
            }
            order._flag = (pos > 0) ? 1 : 0;
        }
        broker->Sell(run_id, strategy, symbol, order, [symbol, this](const TradeReport& report) {
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

