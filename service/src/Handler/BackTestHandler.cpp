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
#include "Strategy.h"
#include "Util/string_algorithm.h"
#include "std_header.h"

BackTestHandler::BackTestHandler(Server* server):HttpHandler(server) {

}

void BackTestHandler::post(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    String strScript = params.at("script");
    if (strScript.empty()) {
        res.status = 400;
        return;
    }
    nlohmann::json script = nlohmann::json::parse(strScript);
    if (script.is_discarded()) {
        res.status = 400;
        return;
    }
    String strategyName = script["id"];
    auto strategySys = _server->GetStrategySystem();
    if (!std::filesystem::exists("scripts")) {
        res.status = 404;
        res.set_content("{message: 'strategy not found.'}", "application/json");
        return;
    }

    auto exchange = (StockHistorySimulation*)_server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM);
    if (!exchange) {
        res.status = 400;
        res.set_content("{message: 'mode[SIM] is not correct.'}", "application/json");
        return;
    }
    
    // Parse Script
    auto graph = parse_strategy_script_v2(script, _server);
    auto sorted_nodes = topo_sort(graph);
    // convert to executable stream

    if (strategySys->HasStrategy(strategyName)) {
        strategySys->DeleteStrategy(strategyName);
    }
    // strategySys->AddStrategy(si);
    // 注册统计信息
    Set<String> featureCollections;
    strategySys->Init(strategyName, sorted_nodes);

    // 驱动数据
    exchange->Login(AccountType::MAIN);
    strategySys->Run(strategyName);
    // 等待数据驱动结束
    while (exchange->IsLogin() && !_server->IsExit()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // TODO: 获取进度
        // double progress = exchange->Progress();
        // INFO("{}%", progress * 100);
    }
    // 获取结果
    nlohmann::json results;
    auto& features = results["features"];
    auto brokerSystem = _server->GetBrokerSubSystem();
    auto indicators = strategySys->GetIndicators(strategyName);
    for (auto& item: indicators) {
        String key(brokerSystem->GetIndicatorName(item.first));
        features[key] = std::get<float>(item.second);
    }
    
    auto symbols = strategySys->GetPools(strategyName);
    for (auto& symbol: symbols) {
        auto str = get_symbol(symbol);
        auto& trades = brokerSystem->GetHistoryTrades(symbol);
        for (auto& trans: trades) {
            String side = (trans._order._side == 0? "buy": "sell");
            for (auto& report: trans._deal._reports) {
                nlohmann::json item;
                item = {str, report._time, report._quantity, report._price};
                results[side].emplace_back(std::move(item));
            }
        }
    }
    
    for (auto node: graph) {
        delete node;
    }
    res.status = 200;
    INFO("{}", results.dump());
    res.set_content(results.dump(), "application/json");
}
