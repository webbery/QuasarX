#include "MarketTiming/ImmediateTiming.h"
#include "DataContext.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"

bool ImmediateTiming::processSignal(const String& strategy, const TradeSignal& signal, const DataContext& context)
{
    auto broker = _server->GetBrokerSubSystem();
    auto symbol = signal.GetSymbol();

    // 捕获信号触发时的时间 (回测模式下使用)
    time_t signalTime = signal.GetBacktestTime();

    // 获取价格：回测模式优先使用 context 中的价格，实盘模式也从 context 获取实时行情
    double price = signal.GetPrice();
    if (_server->GetRunningMode() == RuningType::Backtest) {
        const QuoteInfo* quote = context.GetQuote(symbol);
        if (quote && quote->_close > 0) {
            price = quote->_close;
        }
    } else {
        // 实盘模式：signal 的 price 默认为 0，需要从 context 获取实时行情
        const QuoteInfo* quote = context.GetQuote(symbol);
        if (quote && quote->_close > 0) {
            price = quote->_close;
        }
    }

    // TODO: 初始化订单
    Order order;
    order._price = price;
    order._volume = signal.GetQuantity();
    auto run_id = context.getBacktestRunId();

    // 策略的两融配置：决定本次委托是不是信用委托（融资买入 / 融券卖出 / 买券还券）。
    // 信用类型写进 Order._creditType，由回测成交链路（HistorySimulationBase::OrderReport）
    // 读它来决定是否允许开出负仓、以及是否回写融资余额/在借股数。
    double marginRatio = 1.0;
    bool allowShort = false;
    if (auto* capital = broker->GetStrategyCapital(strategy)) {
        const auto& cfg = capital->getMarginConfig();
        marginRatio = cfg._initialMarginRatio;
        allowShort = cfg._allowShort;
    }

    switch (signal.GetAction()) {
    case TradeAction::BUY:
        order._side = 0;
        // 有空仓 → 平空(_flag=1)，无仓 → 开多(_flag=0)
        {
            int64_t pos = 0;
            auto* histExchange = dynamic_cast<HistorySimulationBase*>(
                _server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
            if (histExchange && context.getBacktestRunId()) {
                pos = histExchange->GetPositionQuantity(symbol);
            }
            order._flag = (pos < 0) ? 1 : 0;
            // 信用类型：持空仓 → 买券还券；否则按保证金比例判定是否融资买入
            if (pos < 0) {
                order._creditType = CreditOrderType::CoverShort;
            } else if (marginRatio < 1.0) {
                order._creditType = CreditOrderType::MarginBuy;
            } else {
                order._creditType = CreditOrderType::None;
            }
        }
        broker->Buy(run_id, strategy, symbol, order, [symbol, this, signalTime](const TradeReport& report) {
            auto sock = Server::GetSocket();
            // 回测模式下使用信号触发时间覆盖 TradeReport._time
            TradeReport fixedReport = report;
            if (signalTime > 0) {
                fixedReport._time = signalTime;
            }
            auto info = to_sse_string(symbol, fixedReport);
            nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
            _reports.emplace_back(std::make_pair(symbol, fixedReport));
            });
    break;
    case TradeAction::SELL:
        order._side = 1;
        // 有多仓 → 平多(_flag=1)，无仓 → 做空(_flag=0)
        {
            int64_t pos = 0;
            auto* histExchange = dynamic_cast<HistorySimulationBase*>(
                _server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
            if (histExchange && context.getBacktestRunId()) {
                pos = histExchange->GetPositionQuantity(symbol);
            }
            order._flag = (pos > 0) ? 1 : 0;
            // 信用类型：无多仓且策略允许做空 → 融券卖出；否则是普通平多
            order._creditType = (pos <= 0 && allowShort) ? CreditOrderType::ShortSell
                                                         : CreditOrderType::None;
        }
        broker->Sell(run_id, strategy, symbol, order, [symbol, this, signalTime](const TradeReport& report) {
            auto sock = Server::GetSocket();
            // 回测模式下使用信号触发时间覆盖 TradeReport._time
            TradeReport fixedReport = report;
            if (signalTime > 0) {
                fixedReport._time = signalTime;
            }
            auto info = to_sse_string(symbol, fixedReport);
            nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
            _reports.emplace_back(std::make_pair(symbol, fixedReport));
            });
    break;
    case TradeAction::EXEC:
    break;
    default:
    break;
    }
    return true;
}
