#include "MarketTiming/ManualTiming.h"
#include "DataContext.h"
#include "Util/log.h"
#include "server.h"
#include "Util/string_algorithm.h"

bool ManualTiming::processSignal(const String& strategy, const TradeSignal& signal,
                                 const DataContext& context) {
    auto action = signal.GetAction();
    if (action == TradeAction::HOLD) return true;

    // 累积决策
    DecisionSnapshot snap;
    snap._symbol = signal.GetSymbol();
    snap._action = action;
    snap._quantity = signal.GetQuantity();
    snap._price = signal.GetPrice();
    snap._flag = (action == TradeAction::SELL) ? 1 : 0;
    snap._epoch = context.GetEpoch();
    _decisions.push_back(snap);

    // 日志
    const char* actionStr = (action == TradeAction::BUY) ? "BUY" : "SELL";
    INFO("[Manual] decision-only: {} {} qty={} price={:.2f} (strategy={})",
         actionStr, get_symbol(signal.GetSymbol()),
         signal.GetQuantity(), signal.GetPrice(), strategy);

    return true;
}

void ManualTiming::SendSummaryEmail(const String& strategy) {
    if (_decisions.empty()) {
        INFO("[Manual] No decisions to notify for strategy {}", strategy);
        return;
    }

    // 构建 SSE 消息（所有决策合并为一条）
    nlohmann::json ssePayload;
    ssePayload["strategy"] = strategy;
    ssePayload["decisions"] = nlohmann::json::array();

    for (const auto& d : _decisions) {
        nlohmann::json decision;
        decision["symbol"] = get_symbol(d._symbol);
        decision["action"] = (d._action == TradeAction::BUY) ? "BUY" : "SELL";
        decision["quantity"] = d._quantity;
        decision["price"] = d._price;
        decision["epoch"] = d._epoch;
        ssePayload["decisions"].push_back(decision);
    }

    Map<String, String> sseData;
    sseData["payload"] = ssePayload.dump();

    auto sock = Server::GetSocket();
    auto msg = format_sse("manual_decision", sseData);
    nng_send(sock, msg.data(), msg.size(), NNG_FLAG_NONBLOCK);

    // 构建邮件正文
    String body = "Strategy: " + strategy + "\n";
    body += "Decisions: " + std::to_string(_decisions.size()) + "\n\n";

    for (const auto& d : _decisions) {
        const char* actionStr = (d._action == TradeAction::BUY) ? "BUY" : "SELL";
        body += fmt::format("{} {} qty={} price={:.2f} epoch={}\n",
                           actionStr, get_symbol(d._symbol),
                           d._quantity, d._price, d._epoch);
    }

    _server->SendEmail(body);

    INFO("[Manual] Sent summary notification for strategy {} with {} decisions", strategy, _decisions.size());

    // 清空累积器
    _decisions.clear();
}