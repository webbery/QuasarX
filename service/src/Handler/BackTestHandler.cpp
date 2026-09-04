#include "Handler/BackTestHandler.h"
#include "Util/MultipartHelper.h"
#include "AgentSubSystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/ETFHistorySimulation.h"
#include "Bridge/exchange.h"
#include "BrokerSubSystem.h"
#include "ExchangeManager.h"
#include "Util/system.h"
#include "Util/datetime.h"
#include "json.hpp"
#include "server.h"
#include <filesystem>
#include <thread>
#include <variant>
#include <algorithm>
#include <atomic>
#include "Strategy.h"
#include "Util/string_algorithm.h"
#include "std_header.h"
#include "nng/nng.h"
#include "AgentSubSystem.h"

namespace {

// 特征计算节点类型集合（快速模式下会被 CacheFeatureNode 替换）
const Set<String> FEATURE_NODE_TYPES = {
    "emd", "cusum", "function", "formula", "breakout", "hmm"
};

// 快速回测模式：重写策略图 JSON
// 1. 找到 XGBoost 节点
// 2. 收集其所有上游特征节点（通过 edges 遍历）
// 3. 从 nodes/edges 中剔除特征节点（保留 QuoteInputNode）
// 4. 插入 CacheFeatureNode，连接到 XGBoost
// 5. 如有 modelPath，覆盖 XGBoost 的 modelFile 参数
void RewriteScriptForFastMode(nlohmann::json& script, const String& cachePath, const String& modelPath) {
    if (!script.contains("nodes") || !script.contains("edges")) {
        WARN("[RewriteForFastMode] script missing nodes/edges, skipping");
        return;
    }

    auto& nodes = script["nodes"];
    auto& edges = script["edges"];

    // 1. 找 XGBoost 节点 ID
    String xgbNodeId;
    for (auto& node : nodes) {
        String nodeType = node.contains("data") ? node["data"].value("nodeType", "") : "";
        if (nodeType == "xgboost") {
            xgbNodeId = node["id"].get<std::string>();
            break;
        }
    }
    if (xgbNodeId.empty()) {
        WARN("[RewriteForFastMode] no XGBoost node found, skipping");
        return;
    }

    // 2. 通过 edges 找 XGBoost 的所有上游节点 ID
    Set<String> upstreamIds;
    for (auto& edge : edges) {
        String target = edge.value("target", "");
        if (target == xgbNodeId) {
            upstreamIds.insert(edge.value("source", ""));
        }
    }

    // 3. 标记要删除的节点（只删特征节点，保留 input 节点）
    Set<String> nodesToRemove;
    for (auto& node : nodes) {
        String nodeId = node["id"].get<std::string>();
        String nodeType = node.contains("data") ? node["data"].value("nodeType", "") : "";
        if (upstreamIds.count(nodeId) && FEATURE_NODE_TYPES.count(nodeType)) {
            nodesToRemove.insert(nodeId);
        }
    }
    // 递归向上：特征节点的上游如果也是特征节点，也要删除
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& edge : edges) {
            String target = edge.value("target", "");
            String source = edge.value("source", "");
            if (nodesToRemove.count(target)) {
                for (auto& n : nodes) {
                    String nid = n["id"].get<std::string>();
                    if (nid == source) {
                        String nt = n.contains("data") ? n["data"].value("nodeType", "") : "";
                        if (FEATURE_NODE_TYPES.count(nt) && !nodesToRemove.count(source)) {
                            nodesToRemove.insert(source);
                            changed = true;
                        }
                    }
                }
            }
        }
    }

    // 4. 删除特征节点
    auto newNodesEnd = std::remove_if(nodes.begin(), nodes.end(), [&](const nlohmann::json& n) {
        return nodesToRemove.count(n["id"].get<std::string>());
    });
    nodes.erase(newNodesEnd, nodes.end());

    // 5. 删除涉及被删节点的边
    auto newEdgesEnd = std::remove_if(edges.begin(), edges.end(), [&](const nlohmann::json& e) {
        return nodesToRemove.count(e.value("source", "")) || nodesToRemove.count(e.value("target", ""));
    });
    edges.erase(newEdgesEnd, edges.end());

    // 6. 插入 CacheFeatureNode
    String cacheNodeId = "9999";
    nlohmann::json cacheNode = {
        {"id", cacheNodeId},
        {"type", "custom"},
        {"position", {{"x", 0}, {"y", 0}}},
        {"data", {
            {"nodeType", "cache_feature"},
            {"label", "缓存特征"},
            {"params", {
                {"cache_path", {{"value", cachePath}}}
            }}
        }}
    };
    nodes.push_back(cacheNode);

    // 7. 插入边：CacheFeatureNode → XGBoostNode
    nlohmann::json cacheEdge = {
        {"id", "e" + cacheNodeId + "->" + xgbNodeId},
        {"source", cacheNodeId},
        {"target", xgbNodeId},
        {"sourceHandle", "output"},
        {"targetHandle", "input"}
    };
    edges.push_back(cacheEdge);

    // 8. 覆盖 XGBoost 的 modelFile（如果提供了 modelPath）
    // modelPath 应为逻辑路径（如 experiments/xxx.json），XGBoostNode 内部拼接 {dbPath}/models/ 前缀
    if (!modelPath.empty()) {
        for (auto& node : nodes) {
            if (node["id"].get<std::string>() == xgbNodeId) {
                node["data"]["params"]["modelFile"]["value"] = modelPath;
                break;
            }
        }
    }

    INFO("[RewriteForFastMode] removed {} feature nodes, inserted CacheFeatureNode -> XGBoost({})",
         nodesToRemove.size(), xgbNodeId);
}

} // anonymous namespace

BackTestHandler::BackTestHandler(Server* server):HttpHandler(server) {

}

void BackTestHandler::post(const httplib::Request& req, httplib::Response& res) {
    // 1. 解析请求参数（支持 JSON 和 multipart 两种格式）
    nlohmann::json script;
    nlohmann::json params;
    bool useValidate = true;
    bool isMultipart = req.is_multipart_form_data();
    if (isMultipart) {
        String mpName;
        String errMsg;
        if (!ParseMultipartScript(req, script, mpName, errMsg)) {
            res.status = 400;
            nlohmann::json err;
            err["message"] = errMsg;
            res.set_content(err.dump(), "application/json");
            return;
        }
        // 上传模型文件到 production 目录
        String strategyNameForModel = script.value("name", "unknown");
        String modelErrMsg;
        if (!ValidateXGBoostModelPaths(script, strategyNameForModel, modelErrMsg)) {
            res.status = 400;
            nlohmann::json err;
            err["message"] = modelErrMsg;
            res.set_content(err.dump(), "application/json");
            return;
        }
        String dbPath = _server->GetConfig().GetDatabasePath();
        if (!WriteMultipartModelFiles(req, strategyNameForModel, dbPath, modelErrMsg)) {
            res.status = 500;
            nlohmann::json err;
            err["message"] = modelErrMsg;
            res.set_content(err.dump(), "application/json");
            return;
        }
    } else {
        try {
            params = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::parse_error& e) {
            res.status = 400;
            res.set_content(R"({"message": "Invalid JSON format"})", "application/json");
            return;
        }

        String strScript = params.value("script", "");
        if (strScript.empty()) {
            res.status = 400;
            res.set_content(R"({"message": "Script is empty"})", "application/json");
            return;
        }

        try {
            script = nlohmann::json::parse(strScript);
        } catch (const nlohmann::json::parse_error& e) {
            res.status = 400;
            String msg = R"({"message": "Script parse error: )" + String(e.what()) + R"("})";
            res.set_content(msg.c_str(), "application/json");
            return;
        }
        useValidate = params.value("validate", true);
    }

    // === 快速回测模式：检测 feature_cache 字段 ===
    {
        // JSON 模式从 params 读取；multipart 模式从 script 顶层字段读取
        String featureCache = isMultipart ? script.value("feature_cache", "") : params.value("feature_cache", "");
        String modelPath = isMultipart ? script.value("model_path", "") : params.value("model_path", "");

        if (!featureCache.empty()) {
            INFO("[Backtest] Fast mode: feature_cache='{}', model_path='{}'", featureCache, modelPath);
            RewriteScriptForFastMode(script, featureCache, modelPath);
        }
    }

    String strategyName = script.value("id", "unknown");
    INFO("[Backtest] Starting backtest for strategy: {}", strategyName);
    double rss_start = getProcessRSS();
    INFO("[Backtest][RSS] start: {:.1f} MB", rss_start);

    // 解析策略资金配置
    double strategyCapital = 0.0;  // 0 表示自动分配
    if (script.contains("capital")) {
        strategyCapital = script["capital"].get<double>();
    }
    
    // 从资金池分配资金
    auto* broker = _server->GetBrokerSubSystem();
    auto* pool = broker ? broker->GetCapitalPool() : nullptr;
    if (pool) {
        if (!pool->allocate(strategyName, strategyCapital)) {
            res.status = 400;
            String msg = R"({"message": "资金分配失败，可用资金不足"})";
            res.set_content(msg.c_str(), "application/json");
            return;
        }
        strategyCapital = pool->get(strategyName).allocated;
        INFO("[Backtest] Capital allocated to {}: {:.0f}", strategyName, strategyCapital);
    }

    // 获取 SSE socket 用于推送进度
    nng_socket sse_sock = _server->GetSocket();

    auto strategySys = _server->GetStrategySystem();
    if (!std::filesystem::exists("scripts")) {
        res.status = 404;
        res.set_content(R"({"message": "Strategy directory not found."})", "application/json");
        return;
    }

    // 收集策略图中所有需要的数据源
    Set<contract_type> requiredSources = collectRequiredSources(script);

    // 使用 ExchangeManager 统一协调
    auto exchangeMgr = _server->GetExchangeManager();
    if (!exchangeMgr) {
        res.status = 400;
        res.set_content(R"({"message": "ExchangeManager not available."})", "application/json");
        return;
    }

    // 2.5 验证策略图的完整性（回测前必须验证）
    if (useValidate) {
        auto [validateSuccess, validateErrorMsg] = _server->ValidateStrategyConfig(script);
        if (!validateSuccess) {
            res.status = 400;
            String msg = R"({"message": "策略图验证失败: )" + String(validateErrorMsg) + R"("})";
            res.set_content(msg.c_str(), "application/json");
            return;
        }
    }

    // 2.6 解析回测时间范围（可选，不配置则使用数据文件的全范围）
    if (script.contains("backtest") && script["backtest"].contains("start") && script["backtest"].contains("end")) {
        String startStr = script["backtest"]["start"].get<std::string>();
        String endStr = script["backtest"]["end"].get<std::string>();

        time_t startT = FromStr(startStr, "%Y-%m-%d");
        time_t endT = FromStr(endStr, "%Y-%m-%d");

        if (startT > 0 && endT > 0 && endT > startT) {
            exchangeMgr->SetBacktestTimeRange(startT, endT);
            INFO("[Backtest] Time range configured: {} ~ {}", startStr, endStr);
        } else {
            WARN("[Backtest] Invalid time range: {} ~ {}, using data range", startStr, endStr);
        }
    }

    INFO("[Backtest] Strategy graph validation passed for: {}", strategyName);

    // 3. 初始化策略
    if (strategySys->HasStrategy(strategyName)) {
        strategySys->DeleteStrategy(strategyName);
    }

    StrategyInitResult initResult;
    try {
        initResult = strategySys->InitStrategy(strategyName, script);
    } catch (const std::exception& e) {
        res.status = 500;
        String msg = R"({"message": "Failed to initialize strategy: )" + String(e.what()) + R"("})";
        res.set_content(msg.c_str(), "application/json");
        WARN("{}", msg);
        return;
    }
    if (!initResult._success) {
        String msg = R"({"message": "Failed to initialize strategy: )" + initResult._errorMessage + R"("})";
        res.status = 500;
        res.set_content(msg.c_str(), "application/json");
        WARN("[Backtest] InitStrategy failed for '{}': {}", strategyName, initResult._errorMessage);
        return;
    }
    INFO("[Backtest][RSS] after InitStrategy: {:.1f} MB", getProcessRSS());

    // 启动需要的 Exchange
    INFO("[Backtest] StartRequiredExchanges (sources={})...", requiredSources.size()); fflush(stdout);
    exchangeMgr->StartRequiredExchanges(requiredSources);
    INFO("[Backtest][RSS] after LoadData: {:.1f} MB", getProcessRSS());

    // 4. 执行回测（多线程版本）
    // 获取策略的标的列表
    auto symbols = strategySys->GetPools(strategyName);
    INFO("[Backtest] GetPools returned {} symbols", symbols.size()); fflush(stdout);

    // 使用 ExchangeManager 创建多 Exchange 回测上下文
    double initialCapital = BACKTEST_INITIAL_CAPITAL;
    INFO("[Backtest] CreateMultiContext (capital={:.0f})...", initialCapital); fflush(stdout);
    run_id_t runId = exchangeMgr->CreateMultiContext(strategyName, symbols, initialCapital);
    INFO("[Backtest] CreateMultiContext done, runId={}", runId); fflush(stdout);

    // 发送进度 (带 run_id)
    SendSSE(sse_sock, "backtest_progress", {{"strategy", strategyName}, {"run_id", std::to_string(runId)}, {"progress", "0.000000"}, {"message", "开始执行回测"}});

    // 启动回测工作线程
    auto* flowSubsystem = strategySys->GetFlowSubsystem();
    if (!flowSubsystem) {
        res.status = 500;
        res.set_content(R"({"message": "FlowSubsystem not available."})", "application/json");
        return;
    }

    // 启动工作线程（使用 exchangeMgr 协调多 Exchange stepForward）
    flowSubsystem->StartBacktestWithExchangeMgr(strategyName, runId, exchangeMgr);

    // 5. 等待回测完成（带进度推送）
    double lastProgress = 0.0;

    // 启动后台线程推送进度
    std::atomic<bool> pushRunning{true};
    std::thread pushThread([this, exchangeMgr, &strategyName, &sse_sock, &lastProgress, &pushRunning, runId]() {
        while (pushRunning && !_server->IsExit()) {
            double progress = exchangeMgr->GetProgress(strategyName);
            if (progress >= 0 && progress - lastProgress >= 0.01) {
                String msg = fmt::format("处理进度：{:.1f}%", progress * 100);
                SendSSE(sse_sock, "backtest_progress", {{"strategy", strategyName}, {"run_id", std::to_string(runId)}, {"progress", std::to_string(0.1 + progress * 0.5)}, {"message", msg}});
                lastProgress = progress;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });

    // 等待回测完成（通过检查 FlowSubsystem 的运行状态）
    int waitCount = 0;
    while (!_server->IsExit()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // 检查回测是否完成
        if (!flowSubsystem->IsRunning(strategyName)) {
            INFO("Backtest completed for strategy {}", strategyName);
            break;
        }
        // 简单等待 5 分钟超时
        if (++waitCount > 3000) {  // 3000 * 100ms = 300s
            WARN("Backtest timeout for strategy {}", strategyName);
            break;
        }
    }
    
    // 回收策略资金
    if (pool) {
        double reclaimed = pool->reclaim(strategyName);
        INFO("[Backtest] Reclaimed {:.0f} from strategy {}", reclaimed, strategyName);
    }

    INFO("[Backtest][RSS] after backtest loop: {:.1f} MB", getProcessRSS());

    // 停止推送线程
    pushRunning = false;
    pushThread.join();

    // 检查回测过程中是否有节点错误
    String lastError = flowSubsystem->GetLastError(strategyName);
    if (!lastError.empty()) {
        SendSSE(sse_sock, "backtest_error", {{"strategy", strategyName}, {"run_id", std::to_string(runId)}, {"error", lastError}});
        WARN("[Backtest] Node error: {}", lastError);
    }

    // 确保最终进度推送
    if (lastProgress < 1.0) {
        String msg = lastError.empty() ? "回测执行完成" : "回测执行出错: " + lastError;
        SendSSE(sse_sock, "backtest_progress", {{"strategy", strategyName}, {"run_id", std::to_string(runId)}, {"progress", "0.700000"}, {"message", msg}});
    }
    exchangeMgr->StopRequiredExchanges(requiredSources);
    INFO("[Backtest][RSS] after StopExchanges: {:.1f} MB", getProcessRSS());
    SendSSE(sse_sock, "backtest_progress", {{"strategy", strategyName}, {"run_id", std::to_string(runId)}, {"progress", "0.800000"}, {"message", "PersistTrades"}});

    // 6. 收集结果
    nlohmann::json results;
    auto& features = results["features"];
    features = nlohmann::json::object();  // 确保 features 始终为 object（无交易时也为 {}，而非 null）
    auto brokerSystem = _server->GetBrokerSubSystem();
    auto indicators = strategySys->GetIndicators(strategyName);

    // 将指标转换为 JSON（只处理 float 类型的指标）
    for (auto& item: indicators) {
        String key(brokerSystem->GetIndicatorName(item.first));
        if (item.second.index() == 0) { // float 类型
            features[key] = std::get<float>(item.second);
        }
    }

    // 持久化交易记录
    brokerSystem->PersistTrades(runId);

    // 7. 获取交易记录并按时间排序（使用 runId 过滤）
    for (auto& symbol: symbols) {
        auto str = get_symbol(symbol);
        auto trades = brokerSystem->GetHistoryTrades(runId, symbol);

        // 收集该股票的所有交易
        std::vector<std::pair<int64_t, nlohmann::json>> buyTrades;
        std::vector<std::pair<int64_t, nlohmann::json>> sellTrades;

        for (const auto& trans: trades) {
            String side = (trans._order._side == 0 ? "buy" : "sell");
            for (const auto& report: trans._deal._reports) {
                nlohmann::json item = {str, report._time, report._quantity, report._price};
                if (side == "buy") {
                    buyTrades.emplace_back(report._time, item);
                } else {
                    sellTrades.emplace_back(report._time, item);
                }
            }
        }

        // 按时间排序
        std::sort(buyTrades.begin(), buyTrades.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        std::sort(sellTrades.begin(), sellTrades.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        // 添加到结果
        for (auto& pr: buyTrades) {
            results["buy"].emplace_back(std::move(pr.second));
        }
        for (auto& pr: sellTrades) {
            results["sell"].emplace_back(std::move(pr.second));
        }
    }

    // 8. 确保 buy 和 sell 字段存在（即使为空数组）
    if (!results.contains("buy")) {
        results["buy"] = nlohmann::json::array();
    }
    if (!results.contains("sell")) {
        results["sell"] = nlohmann::json::array();
    }

    // 9. 添加回测概要信息
    // 从 _collections 中提取性能指标（可能存储为 float(index 0) 或 List<float>(index 1)）
    float sharp_val = 0, annual_return_val = 0, total_return_val = 0;
    float max_drawdown_val = 0, win_rate_val = 0, calmar_val = 0;
    float annual_vol_val = 0;
    for (auto& item : indicators) {
        float val = 0;
        if (item.second.index() == 0) {
            val = std::get<float>(item.second);
        } else {
            // List<float> 取最后一个值（最新值）
            auto& lst = std::get<1>(item.second);
            if (!lst.empty()) val = lst.back();
        }
        switch (item.first) {
        case StatisticIndicator::Sharp:
        case StatisticIndicator::AnualSharp:
            sharp_val = val; break;
        case StatisticIndicator::AnualReturn: annual_return_val = val; break;
        case StatisticIndicator::TotalReturn: total_return_val = val; break;
        case StatisticIndicator::MaxDrawDown: max_drawdown_val = val; break;
        case StatisticIndicator::WinRate: win_rate_val = val; break;
        case StatisticIndicator::Calmar: calmar_val = val; break;
        case StatisticIndicator::AnnualVol: annual_vol_val = val; break;
        default: break;
        }
    }
    results["summary"] = {
        {"strategy_name", strategyName},
        {"buy_count", results["buy"].size()},
        {"sell_count", results["sell"].size()},
        {"indicator_count", features.size()},
        {"sharp", sharp_val},
        {"annual_return", annual_return_val},
        {"annual_volatility", annual_vol_val},
        {"total_return", total_return_val},
        {"max_drawdown", max_drawdown_val},
        {"win_rate", win_rate_val},
        {"calmar_ratio", calmar_val}
    };
    SendSSE(sse_sock, "backtest_progress", {{"strategy", strategyName}, {"run_id", std::to_string(runId)}, {"progress", "0.900000"}, {"message", "add summary"}});

    // === 10. 收集每日收益率数据（从 FlowSubsystem 获取，此时 context 已被销毁）===
    {
        auto dailyReturnsData = flowSubsystem->GetBacktestDailyReturns(strategyName);
        INFO("[Backtest] GetBacktestDailyReturns: dates={}, returns={}",
             dailyReturnsData.dates.size(), dailyReturnsData.returns.size());
        if (!dailyReturnsData.returns.empty() && !dailyReturnsData.dates.empty()) {
            nlohmann::json dailyReturnsArray = nlohmann::json::array();
            nlohmann::json dailyDatesArray = nlohmann::json::array();

            for (size_t i = 0; i < dailyReturnsData.returns.size(); ++i) {
                dailyReturnsArray.emplace_back(dailyReturnsData.returns[i]);
                dailyDatesArray.emplace_back(dailyReturnsData.dates[i]);
            }

            results["daily_returns"] = std::move(dailyReturnsArray);
            results["daily_dates"] = std::move(dailyDatesArray);

            INFO("[Backtest] Daily returns collected: {} data points", dailyReturnsData.returns.size());
        }
    }

    // === 10b. 收集 ProtectionNode 风控触发事件 ===
    {
        auto protEvents = flowSubsystem->GetProtectionEvents(strategyName);
        if (!protEvents.empty()) {
            nlohmann::json eventsArray = nlohmann::json::array();
            for (const auto& evt : protEvents) {
                nlohmann::json item;
                item["bar"] = evt.bar_index;
                item["datetime"] = evt.datetime;
                item["symbol"] = get_symbol(evt.symbol);
                item["type"] = to_string(evt.type);
                item["entry_price"] = evt.entry_price;
                item["current_price"] = evt.current_price;
                eventsArray.emplace_back(std::move(item));
            }
            results["protection_events"] = std::move(eventsArray);
            INFO("[Backtest] Protection events collected: {}", protEvents.size());
        }
    }

    // === 11. 收集蒙特卡洛模拟路径数据（供前端可视化）===
    {
        auto mcPaths = flowSubsystem->GetBacktestMcPaths(strategyName);
        if (!mcPaths.worst_paths.empty() || !mcPaths.best_paths.empty()) {
            nlohmann::json mcPathsJson;

            // 最差路径
            nlohmann::json worstArray = nlohmann::json::array();
            for (const auto& p : mcPaths.worst_paths) {
                nlohmann::json item;
                item["total_return"] = p.total_return;
                item["max_drawdown"] = p.max_drawdown;
                item["win_rate"] = p.win_rate;
                item["longest_win_streak"] = p.longest_win_streak;
                item["longest_loss_streak"] = p.longest_loss_streak;
                item["max_dd_bar_index"] = p.max_dd_bar_index;
                item["vol_ratio"] = p.vol_ratio;
                item["equity_curve"] = nlohmann::json::array();
                for (double v : p.equity_curve) {
                    item["equity_curve"].emplace_back(v);
                }
                worstArray.emplace_back(item);
            }
            mcPathsJson["worst"] = std::move(worstArray);

            // 最好路径
            nlohmann::json bestArray = nlohmann::json::array();
            for (const auto& p : mcPaths.best_paths) {
                nlohmann::json item;
                item["total_return"] = p.total_return;
                item["max_drawdown"] = p.max_drawdown;
                item["win_rate"] = p.win_rate;
                item["longest_win_streak"] = p.longest_win_streak;
                item["longest_loss_streak"] = p.longest_loss_streak;
                item["max_dd_bar_index"] = p.max_dd_bar_index;
                item["vol_ratio"] = p.vol_ratio;
                item["equity_curve"] = nlohmann::json::array();
                for (double v : p.equity_curve) {
                    item["equity_curve"].emplace_back(v);
                }
                bestArray.emplace_back(item);
            }
            mcPathsJson["best"] = std::move(bestArray);

            // 基准路径
            auto serializePathDetail = [](const FlowSubsystem::McPathDetail& p) -> nlohmann::json {
                nlohmann::json item;
                item["total_return"] = p.total_return;
                item["max_drawdown"] = p.max_drawdown;
                item["win_rate"] = p.win_rate;
                item["longest_win_streak"] = p.longest_win_streak;
                item["longest_loss_streak"] = p.longest_loss_streak;
                item["max_dd_bar_index"] = p.max_dd_bar_index;
                item["vol_ratio"] = p.vol_ratio;
                item["equity_curve"] = nlohmann::json::array();
                for (double v : p.equity_curve) {
                    item["equity_curve"].emplace_back(v);
                }
                return item;
            };
            mcPathsJson["median"] = serializePathDetail(mcPaths.median_path);
            mcPathsJson["p10"] = serializePathDetail(mcPaths.p10_path);
            mcPathsJson["p90"] = serializePathDetail(mcPaths.p90_path);

            results["mc_paths"] = std::move(mcPathsJson);

            int totalPaths = mcPaths.worst_paths.size() + mcPaths.best_paths.size() + 3;
            INFO("[Backtest] MonteCarlo paths collected: {} paths", totalPaths);
        }
    }
    SendSSE(sse_sock, "backtest_progress", {{"strategy", strategyName}, {"run_id", std::to_string(runId)}, {"progress", "1.000000"}, {"message", "回测全部完成"}});

    strategySys->ReleaseStrategy(strategyName);
    INFO("[Backtest][RSS] after ReleaseStrategy: {:.1f} MB (delta from start: {:+.1f} MB)",
         getProcessRSS(), getProcessRSS() - rss_start);


    INFO("[Backtest] Completed: {}, {}", features.dump(), results["summary"].dump());
    res.status = 200;
    res.set_content(results.dump(), "application/json");
}
