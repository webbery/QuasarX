#include "MarketTiming/ManualTiming.h"
#include "DataContext.h"
#include "Util/log.h"
#include "Util/datetime.h"
#include "server.h"
#include "Util/string_algorithm.h"
#include "BrokerSubSystem.h"
#include "Bridge/CapitalPool.h"
#include "Decision.h"
#include "Util/EodReport.h"

bool ManualTiming::processSignal(const String& strategy, const TradeSignal& signal,
                                 const DataContext& context) {
    // 决策锁定：非决策 bar 的信号不累积（前置 epoch 用于节点 warmup，
    // SignalNode 评估但不产生最终决策）
    if (!context.IsDecisionBar()) {
        return true;
    }

    // C1 重构：plan 是唯一权威，ManualTiming 不再本地改写 action。
    // ExecuteNode 已经把 plan 翻译成 signal，这里直接累积即可。
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

void ManualTiming::ClearRun() {
    _decisions.clear();
    _riskEvents.clear();
}

void ManualTiming::PushDecisionSse(const DecisionSet& set, const Map<symbol_t, int>& decisionIds) {
    nlohmann::json ssePayload;
    ssePayload["strategy"] = set._strategy;
    ssePayload["as_of"] = set._asOf;
    ssePayload["run_id"] = set._runId;
    ssePayload["decisions"] = buildSseDecisions(set, decisionIds);

    Map<String, String> sseData;
    sseData["payload"] = ssePayload.dump();

    auto sock = Server::GetSocket();
    auto msg = format_sse("manual_decision", sseData);
    nng_send(sock, msg.data(), msg.size(), NNG_FLAG_NONBLOCK);
}

void ManualTiming::SendSummaryEmail(const DecisionSet& set, const EodDebugReport* debugReport) {
    INFO("[Manual] SendSummaryEmail for strategy {} as_of={}, {} items",
         set._strategy, set._asOf, set._items.size());

    // 决策入库（内存 + DuckDB），并回填决策 id 供 SSE 引用。
    // 只有 BUY/SELL 落库：HOLD 不是一笔决策，decisions 表的语义是"待执行指令"。
    Map<symbol_t, int> decisionIds;
    if (auto* broker = _server->GetBrokerSubSystem()) {
        for (const auto& d : set._items) {
            DecisionAction da;
            if (!d.decisionAction(da)) continue;   // HOLD / EXEC
            decisionIds[d._symbol] = broker->AddDecision(set._strategy, d._symbol, da,
                                                         d._quantity, d._price, d._epoch);
        }
    }

    // SSE：动作码与入库用的是同一份派生逻辑
    PushDecisionSse(set, decisionIds);

    auto buys   = set.ofAction(TradeAction::BUY);
    auto sells  = set.ofAction(TradeAction::SELL);
    auto holds  = set.ofAction(TradeAction::HOLD);

    Vector<const DecisionSet::Item*> evalHolds, defaultHolds;
    for (const auto* d : holds) {
        (d->_isDefaultHold ? defaultHolds : evalHolds).push_back(d);
    }

    // ==== 计算决策金额 + 资金信息 ====
    // 组内总金额（同 action 求和）
    double totalBuyValue  = 0.0;
    double totalSellValue = 0.0;
    for (auto* d : buys)  totalBuyValue  += d->_quantity * d->_price;
    for (auto* d : sells) totalSellValue += d->_quantity * d->_price;
    double netValue = totalBuyValue - totalSellValue;

    // 资金口径全部来自 DecisionSet，邮件不再自己去 broker 摸 StrategyCapital。
    // _budgetKnown=false 表示账本缺失：此时降级（不显示资金行、不算占比），
    // 而不是显示 ¥0 让读者以为"钱花光了"。
    const double strategyCap = set._allocated;
    const double strategyAvailable = set._available;
    const bool budgetKnown = set._budgetKnown;

    // 单决策值格式化辅助
    auto fmtMoney = [](double v) {
        return fmt::format("{:.2f}", v);
    };

    // ==== 构建 HTML 邮件 ====
    // 日期用 DecisionSet 的归属日（回放口径），不用 wall clock：
    // 补跑历史日期时邮件正文与落盘/DB 必须说的是同一天。
    String dateStr = set._asOf.empty() ? String("") : set._asOf;

    String html;
    html += R"h(<!DOCTYPE html><html><head><meta charset="UTF-8"></head><body>)h";
    html += R"h(<div style="max-width:680px;margin:0 auto;font-family:-apple-system,'PingFang SC','Microsoft YaHei',sans-serif;color:#1a1a2e;">)h";

    // ── 表头 ──
    html += R"h(<div style="background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;padding:20px 24px;border-radius:12px 12px 0 0;">)h";
    html += fmt::format(R"h(<h2 style="margin:0 0 6px;font-size:18px;">📊 {}</h2>)h", set._strategy);
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

    // 预算可信且为正才展示资金行与占比。账本缺失（budgetKnown=false）时整体降级，
    // 不显示 ¥0 —— 那会让读者以为钱被花光了，而事实是"查不到"。
    const bool showBudget = budgetKnown && strategyCap > 0.0;
    if (showBudget) {
        htmlItem("分配资金", "¥" + fmtMoney(strategyCap));
        htmlItem("可用资金", "¥" + fmtMoney(strategyAvailable));
    } else if (!budgetKnown) {
        htmlItem("资金账本", "未注册（无法核对预算）", true);
    }
    auto buyPct = showBudget ? fmt::format(" ({:.1f}%)", totalBuyValue / strategyCap * 100.0) : "";
    auto sellPct = showBudget ? fmt::format(" ({:.1f}%)", totalSellValue / strategyCap * 100.0) : "";
    htmlItem("买入总额", "¥" + fmtMoney(totalBuyValue) + buyPct);
    htmlItem("卖出总额", "¥" + fmtMoney(totalSellValue) + sellPct);

    auto netSign = netValue >= 0 ? "+" : "-";
    auto netColor = netValue >= 0 ? "#22c55e" : "#ef4444";
    auto netPct = showBudget ? fmt::format(" ({:+.1f}%)", netValue / strategyCap * 100.0) : "";
    html += fmt::format(R"h(<div style="display:flex;justify-content:space-between;align-items:center;padding:8px 12px;background:#f8fafc;border-radius:8px;grid-column:1/-1;">)h");
    html += fmt::format(R"h(<span style="font-size:12px;color:#64748b;">净买入</span>)h");
    html += fmt::format(R"h(<span style="font-size:14px;font-weight:600;color:{};">{}¥{}{}</span>)h", netColor, netSign, fmtMoney(std::abs(netValue)), netPct);
    html += "</div></div>";

    // ── 组合市值 ──
    if (set._portfolioValue > 0.0) {
        auto portPct = showBudget ? fmt::format(" ({:.1f}% of capital)", set._portfolioValue / strategyCap * 100.0) : "";
        html += R"h(<div style="margin-top:12px;padding:12px 16px;background:linear-gradient(135deg,#f0f9ff,#e0f2fe);border-radius:8px;border:1px solid #bae6fd;display:flex;justify-content:space-between;align-items:center;">)h";
        html += R"h(<span style="font-size:13px;font-weight:500;color:#0369a1;">📈 组合总市值</span>)h";
        html += fmt::format(R"h(<div><span style="font-size:18px;font-weight:700;color:#0c4a6e;">¥{}</span><span style="font-size:12px;color:#0284c7;margin-left:6px;">{}</span></div>)h",
                            fmtMoney(set._portfolioValue), portPct);
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

        auto emitHtmlRow = [&](const String& action, const DecisionSet::Item* d, double groupTotal, const String& holdTag = "") {
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

    // ── 风控事件（由 ProtectionNode 触发，价格为原始价） ──
    // 事件来自 DecisionSet：装配时已按本轮裁剪，不会把历史事件当"今日"重复列出。
    if (!set._riskEvents.empty()) {
        html += R"h(<div style="padding:16px 24px;">)h";
        html += R"h(<div style="font-size:12px;font-weight:600;color:#64748b;text-transform:uppercase;letter-spacing:.5px;margin-bottom:10px;">今日风控事件</div>)h";

        for (const auto& event : set._riskEvents) {
            String triggerStr = to_string(event._type);
            html += fmt::format(R"h(<div style="padding:10px 14px;background:#fef2f2;border:1px solid #fecaca;border-radius:8px;margin-bottom:8px;">)h");
            html += fmt::format(R"h(<div style="display:flex;align-items:center;gap:8px;margin-bottom:6px;">)h");
            html += fmt::format(R"h(<span style="display:inline-block;padding:2px 8px;border-radius:4px;font-size:11px;font-weight:600;background:#fee2e2;color:#dc2626;">{}</span>)h", triggerStr);
            html += fmt::format(R"h(<span style="font-size:14px;font-weight:600;color:#1a1a2e;">{}</span>)h", get_symbol(event._symbol));
            html += "</div>";
            html += fmt::format(R"h(<div style="font-size:12px;color:#64748b;">)h");
            html += fmt::format(R"h(<span>成本: {:.2f}</span>)h", event._entry_price);
            if (event._current_price > 0) {
                html += fmt::format(R"h(<span style="margin-left:12px;">当前: {:.2f}</span>)h", event._current_price);
                if (event._entry_price > 0) {
                    double pnl = (event._current_price - event._entry_price) / event._entry_price * 100.0;
                    String pnlColor = pnl >= 0 ? "#22c55e" : "#ef4444";
                    html += fmt::format(R"h(<span style="margin-left:12px;color:{};">盈亏: {:+.2f}%</span>)h", pnlColor, pnl);
                }
            }
            if (event._quantity > 0) {
                html += fmt::format(R"h(<span style="margin-left:12px;">数量: {}</span>)h", event._quantity);
            }
            html += "</div>";
            html += "</div>";
        }

        html += "</div>";
    }

    // ── 调试报告区块（如果有） ──
    if (debugReport && !debugReport->htmlSection.empty()) {
        html += debugReport->htmlSection;
    }

    // ── 页脚 ──
    html += R"h(<div style="padding:12px 24px;background:#f8fafc;font-size:11px;color:#94a3b8;text-align:center;border-radius:0 0 12px 12px;">QuasarX · 量化交易决策通知 · 自动生成</div>)h";
    html += "</div></body></html>";

    // ── 发送邮件（带附件） ──
    if (debugReport && !debugReport->attachments.empty()) {
        String subject = fmt::format("[QuasarX] {} 日终决策 {} · BUY{}/SELL{}",
                                     set._strategy,
                                     set._asOf.empty() ? ToString(Now(), "%Y-%m-%d") : set._asOf,
                                     buys.size(), sells.size());
        _server->SendMailWithAttachments(html, true, debugReport->attachments, subject);
    } else {
        _server->SendHtmlEmail(html);
    }

    INFO("[Manual] Sent summary notification for strategy {} (BUY={}, SELL={}, HOLD={} [eval={}, default={}], risk_events={})",
         set._strategy, buys.size(), sells.size(), holds.size(),
         evalHolds.size(), defaultHolds.size(), set._riskEvents.size());
}
