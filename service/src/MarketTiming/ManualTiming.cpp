#include "MarketTiming/ManualTiming.h"
#include "DataContext.h"
#include "Util/log.h"
#include "Util/datetime.h"
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

    // ── 新增：持仓过滤（避免重复建仓） ────────────────────────
    // 当 OrderDesk 已经回写过手动成交（Fix #2），_portfolio 中已有持仓；
    // 此时如果 SignalNode 仍给 BUY，转 HOLD 让邮件显示"已持仓"。
    // Why: 历史 bug 是 ManualTiming 完全不看 _portfolio，第二天建议 BUY
    //      与已有持仓冲突。Fix #1 注册 CapitalPool 后，_portfolio 的
    //      GetHolding 是真实状态——直接读即可。
    if (action == TradeAction::BUY) {
        auto* broker = _server->GetBrokerSubSystem();
        int64_t currentQty = broker->GetHoldingQuantity(strategy, signal.GetSymbol());
        if (currentQty > 0) {
            INFO("[Manual] {} already hold {} shares, BUY → HOLD",
                 get_symbol(signal.GetSymbol()), currentQty);
            action = TradeAction::HOLD;
        }
    }

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

nlohmann::json ManualTiming::SendSummaryEmail(const String& strategy, double strategyCapital,
                                              double portfolioValue) {
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

    // 策略分配资金（CapitalPool 优先，未注册时 fallback 到 flow._capital）
    double strategyCap = 0.0;
    double strategyAvailable = 0.0;
    auto* pool = _server->GetBrokerSubSystem()->GetCapitalPool();
    if (pool && pool->hasStrategy(strategy)) {
        auto info = pool->get(strategy);
        strategyCap       = info.allocated;
        strategyAvailable = info.available;
    } else if (strategyCapital > 0.0) {
        strategyCap = strategyCapital;
        INFO("[Manual] CapitalPool not registered for '{}', using flow._capital={:.0f} as fallback",
             strategy, strategyCapital);
    }

    // 单决策值格式化辅助
    auto fmtMoney = [](double v) {
        return fmt::format("{:.2f}", v);
    };

    // ==== 构建 HTML 邮件 ====
    auto now = Now();
    String dateStr = ToString(now, "%Y-%m-%d %H:%M");

    String html;
    html += R"h(<!DOCTYPE html><html><head><meta charset="UTF-8"></head><body>)h";
    html += R"h(<div style="max-width:680px;margin:0 auto;font-family:-apple-system,'PingFang SC','Microsoft YaHei',sans-serif;color:#1a1a2e;">)h";

    // ── 表头 ──
    html += R"h(<div style="background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;padding:20px 24px;border-radius:12px 12px 0 0;">)h";
    html += fmt::format(R"h(<h2 style="margin:0 0 6px;font-size:18px;">📊 {}</h2>)h", strategy);
    html += fmt::format(R"h(<div style="font-size:13px;color:#94a3b8;">{} · 日终决策报告</div>)h", dateStr);
    html += R"h(<div style="display:flex;gap:8px;margin-top:12px;">)h";
    html += fmt::format(R"h(<span style="background:rgba(34,197,94,.15);color:#22c55e;padding:4px 12px;border-radius:20px;font-size:13px;">● BUY {}</span>)h", buys.size());
    html += fmt::format(R"h(<span style="background:rgba(239,68,68,.15);color:#ef4444;padding:4px 12px;border-radius:20px;font-size:13px;">● SELL {}</span>)h", sells.size());
    html += fmt::format(R"h(<span style="background:rgba(148,163,184,.15);color:#94a3b8;padding:4px 12px;border-radius:20px;font-size:13px;">● HOLD {}</span>)h", holds.size());
    html += R"h(</div></div>)h";

    // ── 警告 ──
    if (buys.empty() && sells.empty()) {
        if (!defaultHolds.empty() && evalHolds.empty()) {
            html += R"h(<div style="margin:16px 24px 0;padding:10px 14px;background:#fefce8;border:1px solid #fde68a;border-radius:8px;font-size:12px;color:#92400e;">)h";
            html += "⚠️ 所有 HOLD 均为默认补充（SignalNode 未产生任何信号）。<br>可能原因：预热期未完成或特征数据不足。</div>";
        } else if (!evalHolds.empty() && defaultHolds.empty()) {
            html += R"h(<div style="margin:16px 24px 0;padding:10px 14px;background:#f0fdf4;border:1px solid #bbf7d0;border-radius:8px;font-size:12px;color:#166534;">)h";
            html += "✅ 所有 HOLD 均经 SignalNode 评估（今日无交叉信号）。</div>";
        }
    }

    // ── 资金概况 ──
    html += R"h(<div style="padding:16px 24px;">)h";
    html += R"h(<div style="font-size:12px;font-weight:600;color:#64748b;text-transform:uppercase;letter-spacing:.5px;margin-bottom:10px;">资金概况</div>)h";
    html += R"h(<div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;">)h";

    auto htmlItem = [&](const String& label, const String& value, bool full = false) {
        html += fmt::format(R"h(<div style="display:flex;justify-content:space-between;align-items:center;padding:8px 12px;background:#f8fafc;border-radius:8px;{}">)h",
                            full ? "grid-column:1/-1" : "");
        html += fmt::format(R"h(<span style="font-size:12px;color:#64748b;">{}</span>)h", label);
        html += fmt::format(R"h(<span style="font-size:14px;font-weight:600;">{}</span>)h", value);
        html += "</div>";
    };

    if (strategyCap > 0.0) {
        htmlItem("分配资金", "¥" + fmtMoney(strategyCap));
        htmlItem("可用资金", "¥" + fmtMoney(strategyAvailable));
    }
    auto buyPct = strategyCap > 0.0 ? fmt::format(" ({:.1f}%)", totalBuyValue / strategyCap * 100.0) : "";
    auto sellPct = strategyCap > 0.0 ? fmt::format(" ({:.1f}%)", totalSellValue / strategyCap * 100.0) : "";
    htmlItem("买入总额", "¥" + fmtMoney(totalBuyValue) + buyPct);
    htmlItem("卖出总额", "¥" + fmtMoney(totalSellValue) + sellPct);

    auto netSign = netValue >= 0 ? "+" : "-";
    auto netColor = netValue >= 0 ? "#22c55e" : "#ef4444";
    auto netPct = strategyCap > 0.0 ? fmt::format(" ({:+.1f}%)", netValue / strategyCap * 100.0) : "";
    html += fmt::format(R"h(<div style="display:flex;justify-content:space-between;align-items:center;padding:8px 12px;background:#f8fafc;border-radius:8px;grid-column:1/-1;">)h");
    html += fmt::format(R"h(<span style="font-size:12px;color:#64748b;">净买入</span>)h");
    html += fmt::format(R"h(<span style="font-size:14px;font-weight:600;color:{};">{}¥{}{}</span>)h", netColor, netSign, fmtMoney(std::abs(netValue)), netPct);
    html += "</div></div>";

    // ── 组合市值 ──
    if (portfolioValue > 0.0) {
        auto portPct = strategyCap > 0.0 ? fmt::format(" ({:.1f}% of capital)", portfolioValue / strategyCap * 100.0) : "";
        html += R"h(<div style="margin-top:12px;padding:12px 16px;background:linear-gradient(135deg,#f0f9ff,#e0f2fe);border-radius:8px;border:1px solid #bae6fd;display:flex;justify-content:space-between;align-items:center;">)h";
        html += R"h(<span style="font-size:13px;color:#0369a1;font-weight:500;">📈 组合总市值</span>)h";
        html += fmt::format(R"h(<div><span style="font-size:18px;font-weight:700;color:#0c4a6e;">¥{}</span><span style="font-size:12px;color:#0284c7;margin-left:6px;">{}</span></div>)h",
                            fmtMoney(portfolioValue), portPct);
        html += "</div>";
    }
    html += "</div>";

    // ── 分割线 ──
    html += R"h(<div style="height:1px;background:#e2e8f0;margin:0 24px;"></div>)h";

    // ── 决策明细表 ──
    html += R"h(<div style="padding:16px 24px;">)h";
    html += R"h(<div style="font-size:12px;font-weight:600;color:#64748b;text-transform:uppercase;letter-spacing:.5px;margin-bottom:10px;">决策明细</div>)h";

    if (buys.empty() && sells.empty() && holds.empty()) {
        html += R"h(<p style="text-align:center;color:#94a3b8;padding:24px 0;font-size:13px;">今日无交易决策</p>)h";
    } else {
        html += R"h(<table style="width:100%;border-collapse:collapse;font-size:13px;">)h";
        html += R"h(<thead><tr>)h";
        html += R"h(<th style="text-align:left;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">标的</th>)h";
        html += R"h(<th style="text-align:left;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">方向</th>)h";
        html += R"h(<th style="text-align:right;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">数量</th>)h";
        html += R"h(<th style="text-align:right;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">价格</th>)h";
        html += R"h(<th style="text-align:right;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">金额</th>)h";
        html += R"h(<th style="text-align:right;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">占比</th>)h";
        html += "</tr></thead><tbody>";

        auto actionTag = [](const String& action) -> String {
            if (action == "BUY") return R"h(<span style="display:inline-block;padding:2px 8px;border-radius:4px;font-size:11px;font-weight:600;background:#dcfce7;color:#16a34a;">BUY</span>)h";
            if (action == "SELL") return R"h(<span style="display:inline-block;padding:2px 8px;border-radius:4px;font-size:11px;font-weight:600;background:#fee2e2;color:#dc2626;">SELL</span>)h";
            return R"h(<span style="display:inline-block;padding:2px 8px;border-radius:4px;font-size:11px;font-weight:600;background:#f1f5f9;color:#94a3b8;">HOLD</span>)h";
        };

        auto emitHtmlRow = [&](const String& action, const DecisionSnapshot* d, double groupTotal, const String& holdTag = "") {
            double val = d->_quantity * d->_price;
            double grpPct = (groupTotal > 0.0) ? (val / groupTotal * 100.0) : 0.0;
            bool isHold = !holdTag.empty();
            String opacity = isHold ? " style=\"opacity:0.6\"" : "";
            html += fmt::format("<tr{}>", opacity);
            html += fmt::format(R"h(<td style="padding:8px 10px;border-bottom:1px solid #f1f5f9;font-weight:600;">{}</td>)h", get_symbol(d->_symbol));
            html += fmt::format(R"h(<td style="padding:8px 10px;border-bottom:1px solid #f1f5f9;">{})h", actionTag(action));
            if (isHold) {
                html += fmt::format(R"h(<span style="font-size:10px;color:#94a3b8;margin-left:4px;">{}</span>)h", holdTag);
            }
            html += "</td>";
            if (isHold) {
                html += R"h(<td style="padding:8px 10px;border-bottom:1px solid #f1f5f9;text-align:right;color:#94a3b8;">—</td>)h";
                html += R"h(<td style="padding:8px 10px;border-bottom:1px solid #f1f5f9;text-align:right;color:#94a3b8;">—</td>)h";
                html += R"h(<td style="padding:8px 10px;border-bottom:1px solid #f1f5f9;text-align:right;color:#94a3b8;">—</td>)h";
                html += R"h(<td style="padding:8px 10px;border-bottom:1px solid #f1f5f9;text-align:right;color:#94a3b8;">—</td>)h";
            } else {
                html += fmt::format(R"h(<td style="padding:8px 10px;border-bottom:1px solid #f1f5f9;text-align:right;">{}</td>)h", d->_quantity);
                html += fmt::format(R"h(<td style="padding:8px 10px;border-bottom:1px solid #f1f5f9;text-align:right;">{:.2f}</td>)h", d->_price);
                html += fmt::format(R"h(<td style="padding:8px 10px;border-bottom:1px solid #f1f5f9;text-align:right;font-weight:500;">¥{}</td>)h", fmtMoney(val));
                html += fmt::format(R"h(<td style="padding:8px 10px;border-bottom:1px solid #f1f5f9;text-align:right;color:#64748b;font-size:12px;">{:.1f}%</td>)h", grpPct);
            }
            html += "</tr>";
        };

        for (auto* d : buys)  emitHtmlRow("BUY",  d, totalBuyValue);
        for (auto* d : sells) emitHtmlRow("SELL", d, totalSellValue);
        for (auto* d : evalHolds)    emitHtmlRow("HOLD", d, 0.0, "eval");
        for (auto* d : defaultHolds) emitHtmlRow("HOLD", d, 0.0, "default");

        html += "</tbody></table>";
    }
    html += "</div>";

    // ── 页脚 ──
    html += R"h(<div style="padding:12px 24px;background:#f8fafc;font-size:11px;color:#94a3b8;text-align:center;border-radius:0 0 12px 12px;">QuasarX · 量化交易决策通知 · 自动生成</div>)h";
    html += "</div></body></html>";

    _server->SendHtmlEmail(html);

    INFO("[Manual] Sent summary notification for strategy {} (BUY={}, SELL={}, HOLD={} [eval={}, default={}])",
         strategy, buys.size(), sells.size(), holds.size(),
         evalHolds.size(), defaultHolds.size());

    // 清空累积器
    _decisions.clear();

    return decisionsResult;
}
