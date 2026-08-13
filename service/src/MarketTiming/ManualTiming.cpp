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

    // per-symbol 覆盖，只保留最终决策（含 HOLD）
    DecisionSnapshot snap;
    snap._symbol = signal.GetSymbol();
    snap._action = action;
    snap._quantity = signal.GetQuantity();
    snap._price = signal.GetPrice();
    snap._flag = (action == TradeAction::SELL) ? 1 : 0;
    snap._epoch = context.GetEpoch();
    _decisions[signal.GetSymbol()] = snap;

    // 日志
    const char* actionStr = (action == TradeAction::BUY) ? "BUY" :
                            (action == TradeAction::SELL) ? "SELL" : "HOLD";
    INFO("[Manual] decision-only: {} {} qty={} price={:.2f} (strategy={})",
         actionStr, get_symbol(signal.GetSymbol()),
         signal.GetQuantity(), signal.GetPrice(), strategy);

    return true;
}

void ManualTiming::SendSummaryEmail(const String& strategy) {
    INFO("[Manual] SendSummaryEmail called for strategy {}, decisions count: {}", strategy, _decisions.size());

    auto* broker = _server->GetBrokerSubSystem();

    // 构建 SSE 消息 + 写入 BrokerSubSystem 决策存储
    nlohmann::json ssePayload;
    ssePayload["strategy"] = strategy;
    ssePayload["decisions"] = nlohmann::json::array();

    // 分类统计
    Vector<const DecisionSnapshot*> buys, sells, holds;
    for (const auto& [sym, d] : _decisions) {
        if (d._action == TradeAction::BUY) buys.push_back(&d);
        else if (d._action == TradeAction::SELL) sells.push_back(&d);
        else holds.push_back(&d);

        // 写入决策存储（内存 + DuckDB）—— 仅 BUY/SELL
        if (d._action != TradeAction::HOLD) {
            DecisionAction da = to_decision_action(d._action, static_cast<unsigned char>(d._flag));
            int decisionId = broker->AddDecision(strategy, d._symbol, da,
                                                 d._quantity, d._price, d._epoch);

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
    }

    Map<String, String> sseData;
    sseData["payload"] = ssePayload.dump();

    auto sock = Server::GetSocket();
    auto msg = format_sse("manual_decision", sseData);
    nng_send(sock, msg.data(), msg.size(), NNG_FLAG_NONBLOCK);

    // 构建邮件正文（含 BUY / SELL / HOLD 三部分）
    String body = "Strategy: " + strategy + "\n";
    body += fmt::format("Total: {} (BUY={}, SELL={}, HOLD={})\n\n",
                        _decisions.size(), buys.size(), sells.size(), holds.size());

    if (!buys.empty()) {
        body += "=== BUY ===\n";
        for (auto* d : buys) {
            body += fmt::format("  BUY {} qty={} price={:.2f} epoch={}\n",
                               get_symbol(d->_symbol), d->_quantity, d->_price, d->_epoch);
        }
        body += "\n";
    }

    if (!sells.empty()) {
        body += "=== SELL ===\n";
        for (auto* d : sells) {
            body += fmt::format("  SELL {} qty={} price={:.2f} epoch={}\n",
                               get_symbol(d->_symbol), d->_quantity, d->_price, d->_epoch);
        }
        body += "\n";
    }

    if (!holds.empty()) {
        body += "=== HOLD ===\n";
        for (auto* d : holds) {
            body += fmt::format("  HOLD {} qty={} price={:.2f} epoch={}\n",
                               get_symbol(d->_symbol), d->_quantity, d->_price, d->_epoch);
        }
        body += "\n";
    }

    _server->SendEmail(body);

    INFO("[Manual] Sent summary notification for strategy {} (BUY={}, SELL={}, HOLD={})",
         strategy, buys.size(), sells.size(), holds.size());

    // 清空累积器
    _decisions.clear();
}
