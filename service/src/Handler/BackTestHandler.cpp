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

BackTestHandler::BackTestHandler(Server* server):HttpHandler(server) {

}

void BackTestHandler::post(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    String strategyName = params.at("name");
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
    
    // Load Script
    std::filesystem::path p("scripts");
    auto script_path = p / (strategyName + ".json");
    std::ifstream ifs(script_path.string());
    if (!ifs.is_open()) {
        FATAL("Load Script Fail: {}", script_path.string());
        res.status = 400;
        res.set_content("{message: 'read script fail.'}", "application/json");
        return;
    }
    
    std::stringstream buffer;  
    buffer << ifs.rdbuf();
    ifs.close();
    String script_content = buffer.str();
    nlohmann::json script_json = nlohmann::json::parse(script_content);
    if (script_json.is_discarded()) {
        res.status = 400;
        res.set_content("{message: 'script json not correct.'}", "application/json");
        return;
    }
    
    auto si = parse_strategy_script(script_json);
    si._name = strategyName;
    si._virtual = false;

    QuoteFilter filer;
    for (auto& code : si._pool) {
        filer._symbols.emplace(code);
    }
    exchange->SetFilter(filer);
    String tickLevel = params.at("tick");
    if (tickLevel == "1d") {
        exchange->UseLevel(1);
    }
    else {
        exchange->UseLevel(0);
    }
    // 
    if (strategySys->HasStrategy(strategyName)) {
        strategySys->DeleteStrategy(strategyName);
    }
    strategySys->AddStrategy(si);
    // 注册统计信息
    Set<String> featureCollections, statCollection;
    auto strategySystem = _server->GetStrategySystem();
    auto brokerSystem = _server->GetBrokerSubSystem();
    brokerSystem->CleanAllIndicators();
    strategySystem->ClearCollections(strategyName);
    Map<String, StatisticIndicator> statistics{
        {"SHARP", StatisticIndicator::Sharp},
        {"VAR", StatisticIndicator::VaR},
        {"ES", StatisticIndicator::ES},
        {"MAXDRAWDOWN", StatisticIndicator::MaxDrawDown},
        {"CALMAR", StatisticIndicator::Calmar}
    };
    if (params.contains("static")) {
        // sharp/features
        static const Set<String> features{"MACD", "ATR"};
        
        List<nlohmann::json> stats = params["static"];
        for (auto& statInfo: stats) {
            String statName = statInfo["name"];
            auto key = to_upper(statName);
            if (features.count(key)) {
                strategySystem->RegistCollection(strategyName, key, statInfo["params"]);
                featureCollections.insert(statName);
            }
            else if (statistics.count(key)) {
                brokerSystem->RegistIndicator(statistics[key]);
                statCollection.insert(statName);
            }
        }
    }
    // 驱动数据
    exchange->Login();
    // 等待数据驱动结束
    while (exchange->IsLogin() && !_server->IsExit()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // TODO: 获取进度
    }
    // 获取结果
    nlohmann::json results;
    auto& features = results["features"];
    auto allCols = strategySystem->GetCollections(strategyName);
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
                    else if (std::is_same_v<T, List<double>>) {
                        features[name][symbol] = arg;
                    }
                }, feature);
            }
            
        }
    }
    
    for (auto& name: statCollection) {
        auto value = brokerSystem->GetIndicator(strategyName, statistics[name]);
        features[name] = value;
    }
    //
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
