#include "Handler/OptionHandler.h"
#include "json.hpp"
#include "server.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
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

void OptionHistoryHandler::get(const httplib::Request& req, httplib::Response& res) {
    // 获取参数
    auto id_it = req.params.find("id");
    auto type_it = req.params.find("type");
    auto start_it = req.params.find("start");
    auto end_it = req.params.find("end");

    nlohmann::json error_response;
    if (id_it == req.params.end()) {
        error_response["error"] = "missing parameter: id";
        res.status = 400;
        res.set_content(error_response.dump(), "application/json");
        return;
    }
    if (type_it == req.params.end()) {
        error_response["error"] = "missing parameter: type";
        res.status = 400;
        res.set_content(error_response.dump(), "application/json");
        return;
    }
    if (start_it == req.params.end()) {
        error_response["error"] = "missing parameter: start";
        res.status = 400;
        res.set_content(error_response.dump(), "application/json");
        return;
    }
    if (end_it == req.params.end()) {
        error_response["error"] = "missing parameter: end";
        res.status = 400;
        res.set_content(error_response.dump(), "application/json");
        return;
    }

    String id = id_it->second;
    String type = type_it->second;
    String start = start_it->second;
    String end = end_it->second;

    // 构建 CSV 路径
    auto path = _server->GetConfig().GetDatabasePath();
    String csv_path = path + "/zh/option/" + id + ".csv";

    if (!std::filesystem::exists(csv_path)) {
        error_response["error"] = "option data not found: " + id;
        res.status = 404;
        res.set_content(error_response.dump(), "application/json");
        return;
    }

    // 读取 CSV
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        error_response["error"] = "failed to open file: " + id;
        res.status = 500;
        res.set_content(error_response.dump(), "application/json");
        return;
    }

    // 解析时间范围
    auto parse_timestamp = [](const String& time_str) -> int64_t {
        std::istringstream ss(time_str);
        struct tm tm_buf;
        ss >> std::get_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        return static_cast<int64_t>(mktime(&tm_buf));
    };

    int64_t start_ts = parse_timestamp(start);
    int64_t end_ts = parse_timestamp(end);

    nlohmann::json result = nlohmann::json::array();
    String line;
    bool header_skipped = false;

    try {
        while (std::getline(file, line)) {
            if (!header_skipped) {
                header_skipped = true;
                continue;
            }

            if (line.empty()) continue;

            std::stringstream ss(line);
            String datetime_str, open_str, close_str, high_str, low_str;
            String volume_str, turnover_str, oi_str;

            std::getline(ss, datetime_str, ',');
            std::getline(ss, open_str, ',');
            std::getline(ss, close_str, ',');
            std::getline(ss, high_str, ',');
            std::getline(ss, low_str, ',');
            std::getline(ss, volume_str, ',');
            std::getline(ss, turnover_str, ',');
            std::getline(ss, oi_str, ',');

            // 解析 datetime
            int64_t ts = parse_timestamp(datetime_str);

            // 时间范围过滤
            if (ts < start_ts || ts > end_ts) {
                continue;
            }

            nlohmann::json row;
            row["datetime"] = ts;
            row["open"] = open_str.empty() ? 0.0 : std::stod(open_str);
            row["close"] = close_str.empty() ? 0.0 : std::stod(close_str);
            row["high"] = high_str.empty() ? 0.0 : std::stod(high_str);
            row["low"] = low_str.empty() ? 0.0 : std::stod(low_str);
            row["volume"] = volume_str.empty() ? 0 : std::stol(volume_str);
            row["turnover"] = turnover_str.empty() ? 0.0 : std::stod(turnover_str);
            row["oi"] = oi_str.empty() ? 0 : std::stol(oi_str);

            result.emplace_back(std::move(row));
        }
    }
    catch (const std::exception& e) {
        WARN("解析期权 CSV 异常: {}", e.what());
        error_response["error"] = "parse error: " + String(e.what());
        res.status = 500;
        res.set_content(error_response.dump(), "application/json");
        return;
    }

    res.status = 200;
    res.set_content(result.dump(), "application/json");
}
