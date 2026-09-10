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
    // 决策锁定：非决策 bar 的信号不累积（前置 epoch 用于节点 warmup，
    // SignalNode 评估但不产生最终决策）
    if (!context.IsDecisionBar()) {
        return true;
    }

    auto action = signal.GetAction();

    // per-symbol 覆盖，只保留最终决策（含 HOLD）
    DecisionSnapshot snap;
    snap._symbol = signal.GetSymbol();
    snap._action = action;
    snap._quantity = signal.GetQuantity();
    snap._price = signal.GetPrice();
    snap._flag = (action == TradeAction::SELL) ? 1 : 0;
    snap._epoch = context.GetEpoch();
    snap._signalEvaluated = !signal.IsDefaultHold();
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

    // 分类统计（HOLD 区分"评估产生"与"默认补"）
    Vector<const DecisionSnapshot*> buys, sells, holds, evalHolds, defaultHolds;
    for (const auto& [sym, d] : _decisions) {
        if (d._action == TradeAction::BUY) buys.push_back(&d);
        else if (d._action == TradeAction::SELL) sells.push_back(&d);
        else {
            holds.push_back(&d);
            if (d._signalEvaluated) evalHolds.push_back(&d);
            else defaultHolds.push_back(&d);
        }

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

    // 单决策值格式化辅助（千位分隔）
    auto fmtMoney = [](double v) {
        return fmt::format("{:,.2f}", v);
    };

    // ==== 构建邮件正文（表格格式） ====
    const String sep(50, '=');
    String body;

    // ── 表头 ──
    body += sep + "\n";
    body += fmt::format("  Strategy: {}\n", strategy);
    body += fmt::format("  BUY: {}  |  SELL: {}  |  HOLD: {}\n",
                        buys.size(), sells.size(), holds.size());
    body += sep + "\n\n";

    // 警告：所有 HOLD 都是默认补的 → 上游 SignalNode 可能未评估
    if (!buys.empty() || !sells.empty()) {
        // 有真实 BUY/SELL，不需要警告
    } else if (!defaultHolds.empty() && evalHolds.empty()) {
        body += "WARNING: All HOLD are default (SignalNode produced no signals).\n"
                "  Likely cause: warmup not ready or feature data insufficient.\n\n";
    } else if (!evalHolds.empty() && defaultHolds.empty()) {
        body += "All HOLD evaluated (SignalNode ran, no crossover today).\n\n";
    }

    // ── 资金概况 ──
    if (strategyCap > 0.0) {
        body += fmt::format("Capital:    {:>15}   (Available: {})\n",
                            "¥" + fmtMoney(strategyCap), "¥" + fmtMoney(strategyAvailable));
    }
    body += fmt::format(
        "BUY  total: {:>15}   ({:.1f}% of capital)\n"
        "SELL total: {:>15}   ({:.1f}% of capital)\n"
        "Net:        {}{:>15}   ({:+.1f}% of capital)\n\n",
        "¥" + fmtMoney(totalBuyValue),
        (strategyCap > 0.0 ? totalBuyValue / strategyCap * 100.0 : 0.0),
        "¥" + fmtMoney(totalSellValue),
        (strategyCap > 0.0 ? totalSellValue / strategyCap * 100.0 : 0.0),
        (netValue >= 0 ? "+¥" : "-¥"), fmtMoney(std::abs(netValue)),
        (strategyCap > 0.0 ? netValue / strategyCap * 100.0 : 0.0));

    // ── 决策明细表 ──
    body += "--- Decisions " + String(37, '-') + "\n";
    body += "  Symbol       Action   Qty      Price     Amount      Group%   Cap%\n";
    body += "  " + String(70, '-') + "\n";

    auto emitRow = [&](const String& action, const DecisionSnapshot* d, double groupTotal) {
        double val = d->_quantity * d->_price;
        double grpPct = (groupTotal > 0.0) ? (val / groupTotal * 100.0) : 0.0;
        double capPct = (strategyCap > 0.0) ? (val / strategyCap * 100.0) : 0.0;
        body += fmt::format("  {:<13s}{:<9s}{:>7,}  {:>8.2f}  {:>12s}  {:>6.1f}%  {:>5.1f}%\n",
                            get_symbol(d->_symbol), action,
                            d->_quantity, d->_price,
                            "¥" + fmtMoney(val),
                            grpPct,
                            strategyCap > 0.0 ? capPct : 0.0);
    };

    for (auto* d : buys)  emitRow("BUY",  d, totalBuyValue);
    for (auto* d : sells) emitRow("SELL", d, totalSellValue);

    // HOLD 单独标记（evaluated vs default）
    auto emitHoldRow = [&](const DecisionSnapshot* d, bool evaluated) {
        body += fmt::format("  {:<13s}{:<9s}{:>7,}  {:>8.2f}  {:>12s}   [{}]\n",
                            get_symbol(d->_symbol), "HOLD",
                            d->_quantity, d->_price,
                            "¥" + fmtMoney(d->_quantity * d->_price),
                            evaluated ? "eval" : "default");
    };
    for (auto* d : evalHolds)    emitHoldRow(d, true);
    for (auto* d : defaultHolds) emitHoldRow(d, false);

    body += "  " + String(70, '-') + "\n\n";

    _server->SendEmail(body);

    INFO("[Manual] Sent summary notification for strategy {} (BUY={}, SELL={}, HOLD={} [eval={}, default={}])",
         strategy, buys.size(), sells.size(), holds.size(),
         evalHolds.size(), defaultHolds.size());

    // 清空累积器
    _decisions.clear();

    return decisionsResult;
}
