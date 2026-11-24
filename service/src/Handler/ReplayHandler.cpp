#include "Handler/ReplayHandler.h"
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
    } else if (action == "delete_tick") {
        HandleDeleteTickData(req, res);
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

    // DuckDBLogger 已移除，返回空结果
    nlohmann::json result = nlohmann::json::array();

    res.set_content(result.dump(), "application/json");
}

void ReplayHandler::HandleDeleteTickData(const httplib::Request &req, httplib::Response &res) {
    auto beforeStr = req.get_param_value("before");
    int64_t beforeTs = beforeStr.empty() ? 0 : std::stoll(beforeStr);

    // DuckDBLogger 已移除，直接返回成功
    nlohmann::json result;
    result["deleted"] = true;
    result["before"] = beforeTs;
    res.set_content(result.dump(), "application/json");
}
