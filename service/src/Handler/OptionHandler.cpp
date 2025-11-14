#include "Handler/OptionHandler.h"
#include "json.hpp"
#include "server.h"
#include <filesystem>
#include "Bridge/CTP/CTPSymbol.h"
#include "Bridge/ETFOptionSymbol.h"
#include "Util/log.h"

void OptionHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto path = _server->GetConfig().GetDatabasePath();
    auto optionPath = path + "/zh/option";
    if (!std::filesystem::exists(optionPath)) {
        res.status = 500;
        WARN("{} not exist.", optionPath);
        return;
    }
    auto zhFuture = path + "/future.json";
    std::ifstream ifs;
    ifs.open(zhFuture.c_str());
    std::stringstream buffer;  
    buffer << ifs.rdbuf();
    ifs.close();
    nlohmann::json result;
    nlohmann::json futureMap = nlohmann::json::parse(buffer.str());
    for (auto& files: std::filesystem::directory_iterator(optionPath.c_str())) {
        if (files.is_directory())
            continue;

        auto name = files.path().filename().stem().string();
        auto symbol = to_symbol(name);
        nlohmann::json future;
        int64_t id = 0;
        memcpy(&id, &symbol, sizeof(symbol_t));
        future["id"] = id;
        auto type = CTPObjectName(symbol._opt);
        future["name"] = futureMap[type][1];
        future["symbol"] = get_symbol(symbol);
        result["info"].emplace_back(std::move(future));
    }
    res.status = 200;
    res.set_content(result.dump(), "application/json");
}

void OptionDetailHandler::get(const httplib::Request& req, httplib::Response& res) {
    String code = req.get_param_value("id");
    auto exchange = _server->GetAvaliableStockExchange();
    auto symbol = get_etf_option_symbol(code);
    auto quote = exchange->GetQuote(symbol);

    int freq = 3;   // unit: second
    if (req.has_param("freq")) {
        // 时间频率
        freq = atoi(req.get_param_value("freq").c_str());
    }
    double days = YEAR_DAY;
    if (req.has_param("days")) {
        // 时间范围
        days = atof(req.get_param_value("days").c_str());
    }
    nlohmann::json result;
    // TODO: 计算希腊字母
    res.status = 200;
    res.set_content(result.dump(), "application/json");
}
