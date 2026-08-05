#include "MarketTiming/ManualTiming.h"
#include "DataContext.h"
#include "Util/log.h"
#include "server.h"
#include "Util/string_algorithm.h"
#include "BrokerSubSystem.h"
#include "Decision.h"

bool ManualTiming::processSignal(const String& strategy, const TradeSignal& signal,
                                 const DataContext& context) {
    auto action = signal.GetAction();
    if (action == TradeAction::HOLD) return true;

    // per-symbol 覆盖，只保留最终决策
    DecisionSnapshot snap;
    snap._symbol = signal.GetSymbol();
    snap._action = action;
    snap._quantity = signal.GetQuantity();
    snap._price = signal.GetPrice();
    snap._flag = (action == TradeAction::SELL) ? 1 : 0;
    snap._epoch = context.GetEpoch();
    _decisions[signal.GetSymbol()] = snap;

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

    auto* broker = _server->GetBrokerSubSystem();

    // 构建 SSE 消息 + 写入 BrokerSubSystem 决策存储
    nlohmann::json ssePayload;
    ssePayload["strategy"] = strategy;
    ssePayload["decisions"] = nlohmann::json::array();

    for (const auto& [sym, d] : _decisions) {
        // 写入决策存储（内存 + DuckDB）
        DecisionAction da = to_decision_action(d._action, static_cast<unsigned char>(d._flag));
        int decisionId = broker->AddDecision(strategy, d._symbol, da,
                                             d._quantity, d._price, d._epoch);

        // SSE payload
        nlohmann::json decision;
        decision["id"] = decisionId;
        decision["symbol"] = get_symbol(d._symbol);
        decision["action"] = decision_action_name(da);
        decision["label"] = decision_action_label(da);
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

    for (const auto& [sym, d] : _decisions) {
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
