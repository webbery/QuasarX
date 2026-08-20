#include "MarketTiming/ManualTiming.h"
#include "DataContext.h"
#include "Util/log.h"
#include "server.h"
#include "Util/string_algorithm.h"
#include "BrokerSubSystem.h"
#include "Bridge/CapitalPool.h"
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

nlohmann::json ManualTiming::SendSummaryEmail(const String& strategy) {
    INFO("[Manual] SendSummaryEmail called for strategy {}, decisions count: {}", strategy, _decisions.size());

    auto* broker = _server->GetBrokerSubSystem();

    // 构建 SSE 消息 + 写入 BrokerSubSystem 决策存储
    nlohmann::json ssePayload;
    ssePayload["strategy"] = strategy;
    ssePayload["decisions"] = nlohmann::json::array();

    // 返回给调用方的决策数组（供日终 report / 持仓快照使用，action 兼容 DailyDecisionJson::parseAction）
    nlohmann::json decisionsResult = nlohmann::json::array();

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

            nlohmann::json reportDecision;
            reportDecision["symbol"] = get_symbol(d._symbol);
            reportDecision["action"] = (d._action == TradeAction::BUY) ? "BUY" : "SELL";
            reportDecision["quantity"] = d._quantity;
            reportDecision["price"] = d._price;
            reportDecision["flag"] = d._flag;
            decisionsResult.push_back(reportDecision);
        }
    }

    Map<String, String> sseData;
    sseData["payload"] = ssePayload.dump();

    auto sock = Server::GetSocket();
    auto msg = format_sse("manual_decision", sseData);
    nng_send(sock, msg.data(), msg.size(), NNG_FLAG_NONBLOCK);

    // ==== 计算决策金额 + 资金信息 ====
    // 组内总金额（同 action 求和）
    double totalBuyValue  = 0.0;
    double totalSellValue = 0.0;
    for (auto* d : buys)  totalBuyValue  += d->_quantity * d->_price;
    for (auto* d : sells) totalSellValue += d->_quantity * d->_price;
    double netValue = totalBuyValue - totalSellValue;

    // 策略分配资金（CapitalPool 可选 — 未注册时回退为 0，不显示资金占比）
    double strategyCap = 0.0;
    double strategyAvailable = 0.0;
    auto* pool = _server->GetBrokerSubSystem()->GetCapitalPool();
    if (pool && pool->hasStrategy(strategy)) {
        auto info = pool->get(strategy);
        strategyCap       = info.allocated;
        strategyAvailable = info.available;
    }

    // 单决策值格式化辅助（带 ¥ + 千位分隔）
    auto fmtMoney = [](double v) {
        return fmt::format("{:.2f}", v);
    };

    // 组内占比 + 资金占比 格式化助手
    // 同组占比："26.7% of BUY total"
    // 资金占比："5.4% of capital"（strategyCap == 0 时只显示同组占比）
    auto fmtRatio = [&](double val, double groupTotal) {
        double grpPct = (groupTotal > 0.0) ? (val / groupTotal * 100.0) : 0.0;
        if (strategyCap > 0.0) {
            double capPct = val / strategyCap * 100.0;
            return fmt::format("({:.1f}% of group, {:.1f}% of capital)", grpPct, capPct);
        }
        return fmt::format("({:.1f}% of group)", grpPct);
    };

    // ==== 构建邮件正文 ====
    String body;
    body += "Strategy: " + strategy + "\n";
    body += fmt::format("Total: {} (BUY={}, SELL={}, HOLD={})\n\n",
                        _decisions.size(), buys.size(), sells.size(), holds.size());

    // 资金概况（仅在 CapitalPool 注册了该策略时显示）
    if (strategyCap > 0.0) {
        body += fmt::format("Strategy Capital: ¥{}  (Available: ¥{})\n",
                            fmtMoney(strategyCap), fmtMoney(strategyAvailable));
    }

    // 决策金额汇总（始终显示）
    body += fmt::format(
        "Decision Value:\n"
        "  BUY  total: ¥{}  ({:.1f}% of capital)\n"
        "  SELL total: ¥{}  ({:.1f}% of capital)\n"
        "  Net:        {}{}{:.2f}  ({:+.1f}% of capital)\n\n",
        fmtMoney(totalBuyValue),
        (strategyCap > 0.0 ? totalBuyValue / strategyCap * 100.0 : 0.0),
        fmtMoney(totalSellValue),
        (strategyCap > 0.0 ? totalSellValue / strategyCap * 100.0 : 0.0),
        (netValue >= 0 ? "+" : "-"), "¥", std::abs(netValue),
        (strategyCap > 0.0 ? netValue / strategyCap * 100.0 : 0.0));

    if (!buys.empty()) {
        body += "=== BUY ===\n";
        for (size_t i = 0; i < buys.size(); ++i) {
            auto* d = buys[i];
            double val = d->_quantity * d->_price;
            body += fmt::format("  [{}/{}] {} qty={} @ ¥{:.2f} = ¥{} {} epoch={}\n",
                               i + 1, buys.size(),
                               get_symbol(d->_symbol), d->_quantity, d->_price,
                               fmtMoney(val), fmtRatio(val, totalBuyValue),
                               d->_epoch);
        }
        body += "\n";
    }

    if (!sells.empty()) {
        body += "=== SELL ===\n";
        for (size_t i = 0; i < sells.size(); ++i) {
            auto* d = sells[i];
            double val = d->_quantity * d->_price;
            body += fmt::format("  [{}/{}] {} qty={} @ ¥{:.2f} = ¥{} {} epoch={}\n",
                               i + 1, sells.size(),
                               get_symbol(d->_symbol), d->_quantity, d->_price,
                               fmtMoney(val), fmtRatio(val, totalSellValue),
                               d->_epoch);
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

    return decisionsResult;
}
