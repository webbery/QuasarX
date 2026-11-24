#include "Handler/StrategyLogHandler.h"
#include "Util/datetime.h"
#include <sstream>

// DuckDBLogger 已移除，所有接口返回空结果

void StrategyLogHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto type = get_param(req, "type");

    if (type.empty()) {
        res.status = 400;
        nlohmann::json err;
        err["error"] = "type parameter is required (default/by_symbol/stats/strategies)";
        res.set_content(err.dump(), "application/json");
        return;
    }

    try {
        if (type == "by_symbol") {
            query_by_symbol(req, res);
        } else if (type == "stats") {
            query_stats(req, res);
        } else if (type == "strategies") {
            query_strategies(req, res);
        } else {
            query_default(req, res);
        }
    } catch (const std::exception& e) {
        FATAL("[StrategyLogHandler] Error: {}", e.what());
        res.status = 500;
        nlohmann::json err;
        err["error"] = e.what();
        res.set_content(err.dump(), "application/json");
    }
}

void StrategyLogHandler::del(const httplib::Request& req, httplib::Response& res) {
    res.status = 200;
    nlohmann::json resp;
    resp["message"] = "success";
    resp["deleted_count"] = 0;
    res.set_content(resp.dump(), "application/json");
}

void StrategyLogHandler::query_default(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json json;
    json["type"] = "default";
    json["total"] = 0;
    json["logs"] = nlohmann::json::array();
    res.set_content(json.dump(), "application/json");
}

void StrategyLogHandler::query_by_symbol(const httplib::Request& req, httplib::Response& res) {
    auto symbol = get_param(req, "symbol");
    if (symbol.empty()) {
        res.status = 400;
        nlohmann::json err;
        err["error"] = "symbol parameter is required for type=by_symbol";
        res.set_content(err.dump(), "application/json");
        return;
    }

    nlohmann::json json;
    json["type"] = "by_symbol";
    json["symbol"] = symbol;
    json["total"] = 0;
    json["logs"] = nlohmann::json::array();
    res.set_content(json.dump(), "application/json");
}

void StrategyLogHandler::query_stats(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json json;
    json["type"] = "stats";
    json["total_logs"] = 0;
    json["error_count"] = 0;
    json["warn_count"] = 0;
    json["strategy_counts"] = nlohmann::json::object();
    json["error_strategies"] = nlohmann::json::object();
    res.set_content(json.dump(), "application/json");
}

void StrategyLogHandler::query_strategies(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json json;
    json["type"] = "strategies";
    json["strategies"] = nlohmann::json::array();
    res.set_content(json.dump(), "application/json");
}

std::string StrategyLogHandler::get_param(const httplib::Request& req, const std::string& name) {
    auto val = req.get_param_value(name);
    return val.empty() ? "" : val;
}

int StrategyLogHandler::get_int_param(const httplib::Request& req, const std::string& name, int default_val) {
    auto val = get_param(req, name);
    if (val.empty()) return default_val;

    try {
        return std::stoi(val);
    } catch (...) {
        return default_val;
    }
}
