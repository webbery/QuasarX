#include "Handler/ReplayHandler.h"
#include "Util/DuckDBLogger.h"
#include "Util/string_algorithm.h"
#include "server.h"
#include "json.hpp"

void ReplayHandler::post(const httplib::Request &req, httplib::Response &res) {
    auto action = req.get_param_value("action");
    if (action.empty()) {
        action = "query";
    }

    if (action == "query" || action == "history") {
        HandleTicksQuery(req, res);
    } else {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", "unknown action: " + action}}.dump(), "application/json");
    }
}

void ReplayHandler::HandleTicksQuery(const httplib::Request &req, httplib::Response &res) {
    String symbol = req.get_param_value("symbol");
    String startStr = req.get_param_value("start");
    String endStr = req.get_param_value("end");
    String limitStr = req.get_param_value("limit");

    int64_t start = startStr.empty() ? 0 : std::stoll(startStr);
    int64_t end = endStr.empty() ? INT64_MAX : std::stoll(endStr);
    int limit = limitStr.empty() ? 10000 : std::stoi(limitStr);
    limit = std::min(limit, 100000);  // 上限 10 万

    auto ticks = DuckDBLogger::instance().query_ticks(symbol, start, end, limit);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& tick : ticks) {
        nlohmann::json j;
        j["time"] = tick.timestamp_epoch;
        j["symbol"] = tick.symbol;
        j["open"] = tick.open;
        j["close"] = tick.close;
        j["high"] = tick.high;
        j["low"] = tick.low;
        j["volume"] = tick.volume;
        j["turnover"] = tick.turnover;
        j["value"] = tick.value;
        j["upper"] = tick.upper;
        j["lower"] = tick.lower;
        j["source"] = tick.source;
        j["confidence"] = tick.confidence;
        result.push_back(j);
    }

    res.set_content(result.dump(), "application/json");
}
