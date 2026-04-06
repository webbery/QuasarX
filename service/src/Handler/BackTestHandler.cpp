#include "Handler/BackTestHandler.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/exchange.h"
#include "BrokerSubSystem.h"
#include "Util/system.h"
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

BackTestHandler::BackTestHandler(Server* server):HttpHandler(server) {

}

// 发送 SSE 消息
static void SendSSEProgress(nng_socket& sock, const String& strategy, double progress, const String& message) {
    Map<String, String> data;
    data["strategy"] = strategy;
    data["progress"] = std::to_string(progress);
    data["message"] = message;
    auto msg = format_sse("backtest_progress", data);
    nng_send(sock, msg.data(), msg.size(), NNG_FLAG_NONBLOCK);
}

void BackTestHandler::post(const httplib::Request& req, httplib::Response& res) {
    // 1. 解析请求参数
    nlohmann::json params;
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

    // 2. 解析策略脚本
    nlohmann::json script;
    try {
        script = nlohmann::json::parse(strScript);
    } catch (const nlohmann::json::parse_error& e) {
        res.status = 400;
        String msg = R"({"message": "Script parse error: )" + String(e.what()) + R"("})";
        res.set_content(msg.c_str(), "application/json");
        return;
    }

    String strategyName = script.value("id", "unknown");
    INFO("[Backtest] Starting backtest for strategy: {}", strategyName);

    // 获取 SSE socket 用于推送进度
    nng_socket sse_sock = _server->GetSocket();

    auto strategySys = _server->GetStrategySystem();
    if (!std::filesystem::exists("scripts")) {
        res.status = 404;
        res.set_content(R"({"message": "Strategy directory not found."})", "application/json");
        return;
    }

    auto exchange = (StockHistorySimulation*)_server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM);
    if (!exchange) {
        res.status = 400;
        res.set_content(R"({"message": "Backtest mode [SIM] is not available."})", "application/json");
        return;
    }
    // 发送开始消息
    SendSSEProgress(sse_sock, strategyName, 0.0, "回测开始");

    // 3. 初始化策略
    if (strategySys->HasStrategy(strategyName)) {
        strategySys->DeleteStrategy(strategyName);
    }

    try {
        strategySys->InitStrategy(strategyName, script);
        SendSSEProgress(sse_sock, strategyName, 0.1, "策略初始化完成");
    } catch (const std::exception& e) {
        res.status = 500;
        String msg = R"({"message": "Failed to initialize strategy: )" + String(e.what()) + R"("})";
        res.set_content(msg.c_str(), "application/json");
        return;
    }

    exchange->Login(AccountType::MAIN);
    // 4. 执行回测（多线程版本）
    // 获取策略的标的列表
    auto symbols = strategySys->GetPools(strategyName);

    // 使用 StartBacktest 启动多线程回测
    auto* flowSubsystem = strategySys->GetFlowSubsystem();
    double initialCapital = script.contains("params") && script["params"].contains("initialCapital")
                            ? script["params"]["initialCapital"].get<double>()
                            : 100000.0;

    SendSSEProgress(sse_sock, strategyName, 0.2, "开始执行回测");
    flowSubsystem->Start(strategyName, symbols, initialCapital);

    // 5. 等待回测完成（带进度推送）
    double lastProgress = 0.0;

    // 启动后台线程推送进度
    std::atomic<bool> pushRunning{true};
    std::thread pushThread([this, exchange, &strategyName, &sse_sock, &lastProgress, &pushRunning]() {
        while (pushRunning && !_server->IsExit()) {
            double progress = exchange->Progress(strategyName);
            if (progress >= 0 && progress - lastProgress >= 0.01) {
                String msg = fmt::format("处理进度：{:.1f}%", progress * 100);
                SendSSEProgress(sse_sock, strategyName, 0.3 + progress * 0.5, msg);
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

    // 停止推送线程
    pushRunning = false;
    pushThread.join();

    INFO("Backtest finish {}", lastProgress);

    // 确保最终进度推送
    if (lastProgress < 1.0) {
        SendSSEProgress(sse_sock, strategyName, 0.8, "回测执行完成");
    }
    exchange->Logout(AccountType::MAIN);

    // 获取回测 runId（用于获取交易记录）
    uint16_t runId = flowSubsystem->GetBacktestRunId(strategyName);

    // 6. 收集结果
    nlohmann::json results;
    auto& features = results["features"];
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
    results["summary"] = {
        {"strategy_name", strategyName},
        {"buy_count", results["buy"].size()},
        {"sell_count", results["sell"].size()},
        {"indicator_count", features.size()}
    };
    INFO("add summary");

    strategySys->ReleaseStrategy(strategyName);

    SendSSEProgress(sse_sock, strategyName, 1.0, "回测全部完成");

    INFO("[Backtest] Completed: {}", features.dump());
    res.status = 200;
    res.set_content(results.dump(), "application/json");
}
