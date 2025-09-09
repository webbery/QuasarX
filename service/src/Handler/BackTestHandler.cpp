#include "Handler/BackTestHandler.h"
#include "BrokerSubSystem.h"
#include "server.h"
#include <filesystem>
#include <thread>
#include <variant>
#include "Strategy.h"
#include "TraderSubsystem.h"
#include "Util/string_algorithm.h"

BackTestHandler::BackTestHandler(Server* server):HttpHandler(server) {

}

void BackTestHandler::post(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    String strategyName = params.at("name");
    auto strategySys = _server->GetStrategySystem();
    if (!strategySys->HasStrategy(strategyName)) {
        res.status = 404;
        res.set_content("{message: 'strategy not found.'}", "application/json");
        return;
    }

    if (!std::filesystem::exists("scripts")) {
        res.status = 404;
        res.set_content("{message: 'strategy not found.'}", "application/json");
        return;
    }

    auto exchange = _server->GetExchange(ExchangeType::EX_SIM);
    if (!exchange) {
        res.status = 400;
        res.set_content("{message: 'mode is not correct.'}", "application/json");
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
    // 
    if (strategySys->HasStrategy(strategyName)) {
        strategySys->DeleteStrategy(strategyName);
    }
    strategySys->AddStrategy(si);
    // 注册统计信息
    Set<String> featureCollections;
    auto tradeSystem = _server->GetTraderSystem();
    auto brokerSystem = _server->GetBrokerSubSystem();
    brokerSystem->CleanAllIndicators();
    tradeSystem->ClearCollections();
    if (params.contains("static")) {
        // sharp/features
        Set<String> features{"MACD"};
        Map<String, StatisticIndicator> statistics{
            {"SHARP", StatisticIndicator::Sharp}};
        List<String> stats = params["static"];
        for (auto& name: stats) {
            Vector<String> tokens;
            split(name, tokens, "_");
            if (tokens.empty())
                continue;

            if (features.count(tokens[0])) {
                tradeSystem->RegistCollection(name);
                featureCollections.insert(name);
            }
            else if (statistics.count(tokens[0])) {
                brokerSystem->RegistIndicator(statistics[tokens[0]]);
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
    for (auto& name: featureCollections) {
        auto& features = results["features"];
        auto& colls = tradeSystem->GetCollection(name);
        std::visit([&features, &name](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, float> || std::is_same_v<T, Vector<float>>) {
                features[name] = arg;
            }
        }, colls);
    }
    //
    results["buy"];
    results["sell"];
    
    res.status = 200;
    res.set_content(results.dump(), "application/json");
}
