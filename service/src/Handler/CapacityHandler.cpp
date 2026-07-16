#include "Handler/CapacityHandler.h"
#include "AgentSubSystem.h"
#include "Metric/Capacity.h"
#include "server.h"
#include "Strategy.h"
#include "StrategySubSystem.h"
#include "BrokerSubSystem.h"
#include "ExchangeManager.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/exchange.h"
#include "Util/data.h"
#include "Util/datetime.h"
#include "json.hpp"
#include <fstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <limits>

void CapacityHandler::post(const httplib::Request& req, httplib::Response& res) {
    // ── 1. 解析请求 ──────────────────────────────────────────

    nlohmann::json params;
    try {
        params = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid JSON"})", "application/json");
        return;
    }

    String strategyName = params.value("strategy", "");
    if (strategyName.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"strategy is required"})", "application/json");
        return;
    }

    // 资金范围
    auto cap_range = params.value("capital_range", nlohmann::json::object());
    double min_capital = cap_range.value("min", 100000.0);
    double max_capital = cap_range.value("max", 10000000.0);
    int steps = cap_range.value("steps", 20);

    // 冲击模型
    auto impact = params.value("impact_model", nlohmann::json::object());
    double eta = impact.value("eta", 1.0);
    int adv_window = impact.value("adv_window", 20);

    // 约束
    auto constraints = params.value("constraints", nlohmann::json::object());
    double max_participation = constraints.value("max_participation_rate", 0.05);

    // ── 2. 加载策略 JSON ─────────────────────────────────────

    String scripts_dir = "scripts";
    String script_path = scripts_dir + "/" + strategyName;

    if (!std::filesystem::exists(script_path)) {
        res.status = 404;
        String msg = R"({"message":"Strategy not found: )" + strategyName + R"("})";
        res.set_content(msg, "application/json");
        return;
    }

    nlohmann::json script;
    {
        std::ifstream ifs(script_path);
        if (!ifs.is_open()) {
            res.status = 500;
            res.set_content(R"({"message":"Failed to open strategy file"})", "application/json");
            return;
        }
        try {
            ifs >> script;
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"message":"Strategy JSON parse error"})", "application/json");
            return;
        }
    }

    INFO("[Capacity] Starting capacity scan for strategy: {}", strategyName);

    // ── 3. 运行基准回测（零冲击） ────────────────────────────

    auto strategySys = _server->GetStrategySystem();
    auto exchangeMgr = _server->GetExchangeManager();
    if (!exchangeMgr) {
        res.status = 500;
        res.set_content(R"({"message":"ExchangeManager not available"})", "application/json");
        return;
    }

    // 验证策略图
    auto [validateOk, validateErr] = _server->ValidateStrategyConfig(script);
    if (!validateOk) {
        res.status = 400;
        String msg = R"({"message":")" + validateErr + R"("})";
        res.set_content(msg, "application/json");
        return;
    }

    // 初始化策略
    if (strategySys->HasStrategy(strategyName)) {
        strategySys->DeleteStrategy(strategyName);
    }
    try {
        strategySys->InitStrategy(strategyName, script);
    } catch (const std::exception& e) {
        res.status = 500;
        String msg = String(R"({"message":"Init failed: )") + e.what() + R"("})";
        res.set_content(msg, "application/json");
        return;
    }

    // 收集数据源
    Set<String> requiredSources;
    if (script.contains("nodes")) {
        for (auto& node : script["nodes"]) {
            if (node.contains("data") && node["data"].contains("nodeType") &&
                node["data"]["nodeType"] == "input" &&
                node["data"].contains("params") &&
                node["data"]["params"].contains("source")) {
                requiredSources.insert(node["data"]["params"]["source"]["value"].get<String>());
            }
        }
    }
    if (requiredSources.empty()) requiredSources.insert("股票");

    exchangeMgr->StartRequiredExchanges(requiredSources);

    auto symbols = strategySys->GetPools(strategyName);
    double initialCapital = BACKTEST_INITIAL_CAPITAL;
    run_id_t runId = exchangeMgr->CreateMultiContext(strategyName, symbols, initialCapital);

    // 执行回测
    auto* flowSubsystem = strategySys->GetFlowSubsystem();
    if (!flowSubsystem) {
        res.status = 500;
        res.set_content(R"({"message":"FlowSubsystem not available"})", "application/json");
        return;
    }
    flowSubsystem->StartBacktestWithExchangeMgr(strategyName, runId, exchangeMgr);

    // 等待完成
    int waitCount = 0;
    while (!_server->IsExit()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!flowSubsystem->IsRunning(strategyName)) break;
        if (++waitCount > 3000) {
            WARN("[Capacity] Backtest timeout for {}", strategyName);
            break;
        }
    }

    exchangeMgr->StopRequiredExchanges(requiredSources);

    // ── 4. 提取交易日志 ──────────────────────────────────────

    auto brokerSystem = _server->GetBrokerSubSystem();
    Vector<CapacityTrade> trades;

    for (const auto& symbol : symbols) {
        auto historyTrades = brokerSystem->GetHistoryTrades(runId, symbol);
        for (const auto& trans : historyTrades) {
            for (const auto& report : trans._deal._reports) {
                CapacityTrade ct;
                ct.symbol = trans._order._symbol;
                ct.price = report._price;
                ct.shares = report._quantity;
                ct.side = trans._order._side;
                ct.time = report._time;
                // day_index 从 time 推算（后续对齐）
                ct.day_index = 0;
                trades.push_back(ct);
            }
        }
    }

    // 按时间排序
    std::sort(trades.begin(), trades.end(),
        [](const auto& a, const auto& b) { return a.time < b.time; });

    INFO("[Capacity] Extracted {} trades for {} symbols", trades.size(), symbols.size());

    if (trades.empty()) {
        strategySys->ReleaseStrategy(strategyName);
        res.status = 200;
        res.set_content(R"({"message":"No trades found","capacity_curve":[],"summary":{}})", "application/json");
        return;
    }

    // ── 5. 加载市场数据 + 对齐 day_index ─────────────────────

    Vector<symbol_t> sym_vec(symbols.begin(), symbols.end());
    Map<symbol_t, Vector<double>> adv_data, vol_data;

    if (!Capacity::loadMarketData(sym_vec, adv_window, adv_data, vol_data)) {
        strategySys->ReleaseStrategy(strategyName);
        res.status = 500;
        res.set_content(R"({"message":"Failed to load market data"})", "application/json");
        return;
    }

    // 用 time_t 对齐 day_index
    {
        auto dailyData = flowSubsystem->GetBacktestDailyReturns(strategyName);
        Map<time_t, size_t> date_to_index;
        for (size_t i = 0; i < dailyData.dates.size(); ++i) {
            date_to_index[dailyData.dates[i]] = i;
        }

        for (auto& ct : trades) {
            auto it = date_to_index.find(ct.time);
            if (it != date_to_index.end()) {
                ct.day_index = it->second;
            } else {
                // 找不到精确匹配时，找最近的交易日
                time_t best_diff = std::numeric_limits<time_t>::max();
                for (const auto& [t, idx] : date_to_index) {
                    time_t diff = (t > ct.time) ? (t - ct.time) : (ct.time - t);
                    if (diff < best_diff) {
                        best_diff = diff;
                        ct.day_index = idx;
                    }
                }
            }
        }
    }

    // ── 6. 容量扫描 ──────────────────────────────────────────

    Capacity scanner;
    auto curve = scanner.scan(
        trades, adv_data, vol_data,
        initialCapital, min_capital, max_capital, steps,
        eta, max_participation
    );

    // 基准指标（第一个点）
    nlohmann::json baseline;
    if (!curve.empty()) {
        baseline = {
            {"sharpe", curve[0].sharpe},
            {"total_return", curve[0].total_return},
            {"max_drawdown", curve[0].max_drawdown},
            {"win_rate", curve[0].win_rate},
            {"n_trades", trades.size()}
        };
    }

    // 容量曲线
    nlohmann::json curve_json = nlohmann::json::array();
    for (const auto& pt : curve) {
        nlohmann::json item;
        item["capital"] = pt.capital;
        item["sharpe"] = pt.sharpe;
        item["total_return"] = pt.total_return;
        item["max_drawdown"] = pt.max_drawdown;
        item["win_rate"] = pt.win_rate;
        item["avg_participation"] = pt.avg_participation;
        item["max_participation"] = pt.max_participation;
        item["avg_slippage_bps"] = pt.avg_slippage_bps;
        item["orders_above_limit"] = pt.orders_above_limit;
        item["sharpe_decay"] = pt.sharpe_decay;
        curve_json.emplace_back(item);
    }

    // 摘要
    double baseline_sharpe = curve.empty() ? 0 : curve[0].sharpe;
    auto summary = Capacity::computeSummary(curve, adv_data, baseline_sharpe);

    nlohmann::json summary_json;
    summary_json["capacity_20pct"] = summary.capacity_20pct;
    summary_json["capacity_50pct"] = summary.capacity_50pct;
    summary_json["bottleneck_symbol"] = summary.bottleneck_symbol;
    summary_json["bottleneck_adv"] = summary.bottleneck_adv;

    // ── 7. 返回结果 ──────────────────────────────────────────

    nlohmann::json result;
    result["strategy"] = strategyName;
    result["baseline"] = baseline;
    result["capacity_curve"] = curve_json;
    result["summary"] = summary_json;

    strategySys->ReleaseStrategy(strategyName);

    INFO("[Capacity] Scan complete: {} points, capacity_20%={:.0f}, capacity_50%={:.0f}",
         curve.size(), summary.capacity_20pct, summary.capacity_50pct);

    res.status = 200;
    res.set_content(result.dump(), "application/json");
}
