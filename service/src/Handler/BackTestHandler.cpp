#include "Handler/BackTestHandler.h"
#include "Bridge/SIM/SIMExchange.h"
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
    String strategyName = script["graph"]["id"];
    auto strategySys = _server->GetStrategySystem();
    if (!std::filesystem::exists("scripts")) {
        res.status = 404;
        res.set_content("{message: 'strategy not found.'}", "application/json");
        return;
    }

    auto exchange = (StockSimulation*)_server->GetExchange(ExchangeType::EX_SIM);
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
    strategySys->ClearCollections(strategyName);
    strategySys->Init(strategyName, sorted_nodes);

    strategySys->Run(strategyName);
    // 驱动数据
    exchange->Login(AccountType::MAIN);
    // 等待数据驱动结束
    while (exchange->IsLogin() && !_server->IsExit()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // TODO: 获取进度
    }
    // 获取结果
    nlohmann::json results;
    auto& features = results["features"];
    auto allCols = strategySys->GetCollections(strategyName);
    for (auto& item: allCols) {
        auto symbol = get_symbol(item.first);
        for (auto& name: featureCollections) {
            auto& colls = item.second.at(name);
            for (auto& feature : colls) {
                std::visit([&features, &name, &symbol](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, double>) {
                        features[name][symbol].emplace_back(arg);
                    }
                    else if constexpr (std::is_same_v<T, Vector<double>>) {
                        features[name][symbol] = arg;
                    }
                    else if constexpr (std::is_same_v<T, Eigen::MatrixXd>) {

                    }
                }, feature);
            }
            
        }
    }
    
    Map<StatisticIndicator, String> statistics{
        {StatisticIndicator::Sharp, "SHARP"},
        {StatisticIndicator::VaR, "VAR"},
        {StatisticIndicator::ES, "ES"},
        {StatisticIndicator::MaxDrawDown, "MAXDRAWDOWN"},
        {StatisticIndicator::Calmar, "CALMAR"}
    };
    auto brokerSystem = _server->GetBrokerSubSystem();
    auto& statCollection = brokerSystem->GetIndicatorsName(strategyName);
    for (auto& indicator: statCollection) {
        auto value = brokerSystem->GetIndicator(strategyName, indicator);
        features[statistics[indicator]] = value;
    }
    
    auto symbols = brokerSystem->GetPoolSymbols(strategyName);
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
    
    res.status = 200;
    INFO("{}", results.dump());
    res.set_content(results.dump(), "application/json");
}
