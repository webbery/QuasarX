#include "Handler/NodeIOHandler.h"
#include "Util/DuckDBLogger.h"
#include "Util/datetime.h"
#include <sstream>

void NodeIOHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        query_default(req, res);
    } catch (const std::exception& e) {
        FATAL("[NodeIOHandler] Error: {}", e.what());
        res.status = 500;
        nlohmann::json err;
        err["error"] = e.what();
        res.set_content(err.dump(), "application/json");
    }
}

void NodeIOHandler::del(const httplib::Request& req, httplib::Response& res) {
    try {
        cleanup_old(req, res);
    } catch (const std::exception& e) {
        FATAL("[NodeIOHandler] Delete error: {}", e.what());
        res.status = 500;
        nlohmann::json err;
        err["error"] = e.what();
        res.set_content(err.dump(), "application/json");
    }
}

void NodeIOHandler::query_default(const httplib::Request& req, httplib::Response& res) {
    if (!DuckDBLogger::instance().is_initialized()) {
        res.status = 503;
        nlohmann::json err;
        err["error"] = "DuckDB logger not initialized";
        res.set_content(err.dump(), "application/json");
        return;
    }

    auto strategy = get_param(req, "strategy");
    auto node_type = get_param(req, "node_type");
    auto start_time = get_param(req, "start_time");
    auto end_time = get_param(req, "end_time");
    int64_t epoch_from = get_int64_param(req, "epoch_from", 0);
    int64_t epoch_to = get_int64_param(req, "epoch_to", 0);
    int limit = get_int_param(req, "limit", 1000);
    int offset = get_int_param(req, "offset", 0);

    auto logs = DuckDBLogger::instance().query_node_io_logs(
        strategy, node_type, epoch_from, epoch_to, start_time, end_time, limit, offset
    );

    int totalCount = DuckDBLogger::instance().count_node_io_logs(
        strategy, node_type, epoch_from, epoch_to, start_time, end_time
    );

    nlohmann::json json;
    json["total"] = totalCount;
    json["logs"] = nlohmann::json::array();

    for (const auto& log : logs) {
        nlohmann::json item;
        item["id"] = log.id;
        item["timestamp"] = log.timestamp;
        item["strategy"] = log.strategy_name;
        item["epoch"] = log.epoch;
        item["node_type"] = log.node_type;
        item["node_id"] = log.node_id;

        if (!log.input_json.empty()) {
            try {
                item["input"] = nlohmann::json::parse(log.input_json);
            } catch (...) {
                item["input"] = log.input_json;
            }
        }
        if (!log.output_json.empty()) {
            try {
                item["output"] = nlohmann::json::parse(log.output_json);
            } catch (...) {
                item["output"] = log.output_json;
            }
        }
        if (!log.metadata_json.empty()) {
            try {
                item["metadata"] = nlohmann::json::parse(log.metadata_json);
            } catch (...) {
                item["metadata"] = log.metadata_json;
            }
        }

        json["logs"].push_back(item);
    }

    res.set_content(json.dump(), "application/json");
}

void NodeIOHandler::cleanup_old(const httplib::Request& req, httplib::Response& res) {
    if (!DuckDBLogger::instance().is_initialized()) {
        res.status = 503;
        nlohmann::json err;
        err["error"] = "DuckDB logger not initialized";
        res.set_content(err.dump(), "application/json");
        return;
    }

    auto before_date = get_param(req, "before_date");
    if (before_date.empty()) {
        res.status = 400;
        nlohmann::json err;
        err["error"] = "before_date parameter is required (format: YYYY-MM-DD or YYYY-MM-DD HH:MM:SS)";
        res.set_content(err.dump(), "application/json");
        return;
    }

    int64_t deleted = DuckDBLogger::instance().delete_node_io_logs_before(before_date);

    nlohmann::json json;
    json["deleted_count"] = deleted;
    json["before_date"] = before_date;

    res.set_content(json.dump(), "application/json");
}

std::string NodeIOHandler::get_param(const httplib::Request& req, const std::string& name) {
    auto val = req.get_param_value(name);
    return val.empty() ? "" : val;
}

int64_t NodeIOHandler::get_int64_param(const httplib::Request& req, const std::string& name, int64_t default_val) {
    auto val = get_param(req, name);
    if (val.empty()) return default_val;
    try {
        return std::stoll(val);
    } catch (...) {
        return default_val;
    }
}

int NodeIOHandler::get_int_param(const httplib::Request& req, const std::string& name, int default_val) {
    auto val = get_param(req, name);
    if (val.empty()) return default_val;
    try {
        return std::stoi(val);
    } catch (...) {
        return default_val;
    }
}
