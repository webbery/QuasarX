#include "Handler/StrategyLogHandler.h"
#include "Util/DuckDBLogger.h"
#include "Util/datetime.h"
#include <sstream>

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
            // default 或其他值，使用默认查询
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

void StrategyLogHandler::query_default(const httplib::Request& req, httplib::Response& res) {
    if (!DuckDBLogger::instance().is_initialized()) {
        res.status = 503;
        nlohmann::json err;
        err["error"] = "DuckDB logger not initialized";
        res.set_content(err.dump(), "application/json");
        return;
    }

    auto strategy = get_param(req, "strategy");
    auto level = get_param(req, "level");
    auto start_time = get_param(req, "start_time");
    auto end_time = get_param(req, "end_time");
    int limit = get_int_param(req, "limit", 1000);
    int offset = get_int_param(req, "offset", 0);

    auto logs = DuckDBLogger::instance().query_strategy_logs(
        strategy, level, start_time, end_time, limit, offset
    );

    // 查询总记录数（不受 limit/offset 限制）
    int totalCount = DuckDBLogger::instance().count_strategy_logs(
        strategy, level, start_time, end_time
    );

    nlohmann::json json;
    json["type"] = "default";
    json["total"] = totalCount;
    json["logs"] = nlohmann::json::array();
    
    for (const auto& log : logs) {
        nlohmann::json item;
        item["id"] = log.id;
        item["timestamp"] = log.timestamp;
        item["strategy"] = log.strategy_name;
        item["level"] = log.level;
        item["message"] = log.message;
        
        if (!log.context_json.empty()) {
            try {
                item["context"] = nlohmann::json::parse(log.context_json);
            } catch (...) {
                item["context"] = log.context_json;
            }
        }
        
        json["logs"].push_back(item);
    }
    
    res.set_content(json.dump(), "application/json");
}

void StrategyLogHandler::query_by_symbol(const httplib::Request& req, httplib::Response& res) {
    if (!DuckDBLogger::instance().is_initialized()) {
        res.status = 503;
        nlohmann::json err;
        err["error"] = "DuckDB logger not initialized";
        res.set_content(err.dump(), "application/json");
        return;
    }
    
    auto symbol = get_param(req, "symbol");
    if (symbol.empty()) {
        res.status = 400;
        nlohmann::json err;
        err["error"] = "symbol parameter is required for type=by_symbol";
        res.set_content(err.dump(), "application/json");
        return;
    }
    
    auto start_time = get_param(req, "start_time");
    auto end_time = get_param(req, "end_time");
    auto level = get_param(req, "level");
    int limit = get_int_param(req, "limit", 500);

    // 查询包含该symbol的策略日志（通过context JSON中的symbol字段）
    // 使用 C API 查询，然后过滤
    auto all_logs = DuckDBLogger::instance().query_strategy_logs(
        "", level, start_time, end_time, limit * 10, 0
    );

    std::vector<StrategyLogEntry> results;

    try {
        for (const auto& log : all_logs) {
            if (log.context_json.empty()) continue;
            
            try {
                auto ctx = nlohmann::json::parse(log.context_json);
                if (ctx.contains("symbol") && ctx["symbol"] == symbol) {
                    results.push_back(log);
                    if (results.size() >= limit) break;
                }
            } catch (...) {
                // JSON解析失败，跳过
                continue;
            }
        }
    } catch (const std::exception& e) {
        FATAL("[StrategyLogHandler] Query by symbol failed: {}", e.what());
        res.status = 500;
        nlohmann::json err;
        err["error"] = e.what();
        res.set_content(err.dump(), "application/json");
        return;
    }
    
    nlohmann::json json;
    json["type"] = "by_symbol";
    json["symbol"] = symbol;
    json["total"] = results.size();
    json["logs"] = nlohmann::json::array();
    
    for (const auto& log : results) {
        nlohmann::json item;
        item["id"] = log.id;
        item["timestamp"] = log.timestamp;
        item["strategy"] = log.strategy_name;
        item["level"] = log.level;
        item["message"] = log.message;
        
        if (!log.context_json.empty()) {
            try {
                item["context"] = nlohmann::json::parse(log.context_json);
            } catch (...) {
                item["context"] = log.context_json;
            }
        }
        
        json["logs"].push_back(item);
    }
    
    res.set_content(json.dump(), "application/json");
}

void StrategyLogHandler::query_stats(const httplib::Request& req, httplib::Response& res) {
    if (!DuckDBLogger::instance().is_initialized()) {
        res.status = 503;
        nlohmann::json err;
        err["error"] = "DuckDB logger not initialized";
        res.set_content(err.dump(), "application/json");
        return;
    }
    
    auto start_time = get_param(req, "start_time");
    auto end_time = get_param(req, "end_time");
    
    auto stats = DuckDBLogger::instance().get_strategy_stats(start_time, end_time);
    
    nlohmann::json json;
    json["type"] = "stats";
    json["total_logs"] = stats.total_logs;
    json["error_count"] = stats.error_count;
    json["warn_count"] = stats.warn_count;
    
    json["strategy_counts"] = nlohmann::json::object();
    for (const auto& [name, count] : stats.strategy_counts) {
        json["strategy_counts"][name] = count;
    }
    
    json["error_strategies"] = nlohmann::json::object();
    for (const auto& [name, count] : stats.error_strategies) {
        json["error_strategies"][name] = count;
    }
    
    res.set_content(json.dump(), "application/json");
}

void StrategyLogHandler::query_strategies(const httplib::Request& req, httplib::Response& res) {
    if (!DuckDBLogger::instance().is_initialized()) {
        res.status = 503;
        nlohmann::json err;
        err["error"] = "DuckDB logger not initialized";
        res.set_content(err.dump(), "application/json");
        return;
    }
    
    // 获取所有有日志的策略列表
    auto stats = DuckDBLogger::instance().get_strategy_stats("", "");
    
    nlohmann::json json;
    json["type"] = "strategies";
    json["strategies"] = nlohmann::json::array();
    
    for (const auto& [name, count] : stats.strategy_counts) {
        nlohmann::json item;
        item["name"] = name;
        item["log_count"] = count;
        
        // 查找该策略的错误数
        if (stats.error_strategies.contains(name)) {
            item["error_count"] = stats.error_strategies.at(name);
        } else {
            item["error_count"] = 0;
        }
        
        json["strategies"].push_back(item);
    }
    
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
