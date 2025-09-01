#include "Handler/BackTestHandler.h"
#include "server.h"
#include <filesystem>
#include "Strategy.h"

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
    // 驱动数据
    exchange->Login();
}
