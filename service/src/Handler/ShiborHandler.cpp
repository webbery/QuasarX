#include "Handler/ShiborHandler.h"
#include "Util/log.h"
#include "server.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

ShiborHandler::ShiborHandler(Server* server): HttpHandler(server) {
}

String ShiborHandler::GetShiborCsvPath() {
    auto db_path = _server->GetConfig().GetDatabasePath();
    return db_path + "/shibor.csv";
}

String ShiborHandler::GetQueryParam(const httplib::Request& req, const String& key, const String& default_val) {
    auto it = req.params.find(key);
    return it != req.params.end() ? it->second : default_val;
}

bool ShiborHandler::ReadCsvData(nlohmann::json& data) {
    String csv_path = GetShiborCsvPath();
    
    if (!fs::exists(csv_path)) {
        return false;
    }
    
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        WARN("无法打开 SHIBOR CSV: {}", csv_path);
        return false;
    }
    
    String line;
    bool header_skipped = false;
    
    try {
        while (std::getline(file, line)) {
            if (!header_skipped) {
                header_skipped = true;
                continue;  // 跳过表头
            }
            
            if (line.empty()) continue;
            
            std::stringstream ss(line);
            String date_str, term, rate_str, change_str;
            
            std::getline(ss, date_str, ',');
            std::getline(ss, term, ',');
            std::getline(ss, rate_str, ',');
            std::getline(ss, change_str, ',');
            
            nlohmann::json item;
            item["date"] = date_str;
            item["term"] = term;
            item["rate"] = rate_str.empty() ? 0.0 : std::stod(rate_str);
            item["change"] = change_str.empty() ? 0.0 : std::stod(change_str);
            
            data.push_back(item);
        }
    }
    catch (const std::exception& e) {
        WARN("解析 SHIBOR CSV 异常: {}", e.what());
        return false;
    }
    
    return !data.empty();
}

String ShiborHandler::GetLatestCachedDate(const nlohmann::json& data) {
    String latest;
    for (const auto& item : data) {
        String d = item.value("date", "");
        if (d > latest) {
            latest = d;
        }
    }
    return latest;
}

bool ShiborHandler::AppendFromPython(const String& date) {
    String csv_path = GetShiborCsvPath();
    String parent_dir = fs::path(csv_path).parent_path().string();
    
    // 确保父目录存在
    fs::create_directories(parent_dir);
    
    // 构建命令
    String cmd = fmt::format("python tools/fetch_shibor.py --date {} --save-dir {} --append-csv",
                            date, parent_dir);
    
    String output;
    bool success = RunCommand(cmd, output);
    
    if (!success) {
        WARN("调用 Python 脚本失败: {}, 输出: {}", cmd, output);
    }
    
    return success;
}

nlohmann::json ShiborHandler::FilterByDate(const nlohmann::json& data, const String& date) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& item : data) {
        if (item.value("date", "") == date) {
            result.push_back(item);
        }
    }
    return result;
}

nlohmann::json ShiborHandler::FilterByTerm(const nlohmann::json& data, const String& term) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& item : data) {
        String t = item.value("term", "");
        if (t == term) {
            result.push_back(item);
        }
    }
    return result;
}

void ShiborHandler::get(const httplib::Request& req, httplib::Response& res) {
    String date = GetQueryParam(req, "date");
    String days_str = GetQueryParam(req, "days", "1");
    String term = GetQueryParam(req, "term");
    
    nlohmann::json response;
    
    try {
        int days = std::stoi(days_str);
        days = std::clamp(days, 1, 365);
        
        // 读取 CSV 缓存
        nlohmann::json all_data;
        bool cache_ok = ReadCsvData(all_data);
        
        // 获取最新日期
        String latest_date = GetLatestCachedDate(all_data);
        
        String target_date = date.empty() ? latest_date : date;
        
        if (target_date.empty() || !cache_ok) {
            // 无缓存，拉取今天
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            struct tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &time_t_now);
#else
            localtime_r(&time_t_now, &tm_buf);
#endif
            char date_buf[11];
            std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_buf);
            target_date = date_buf;
            INFO("SHIBOR 无缓存，正在拉取: {}", target_date);
            bool fetch_ok = AppendFromPython(target_date);
            if (!fetch_ok) {
                response["status"] = "error";
                response["code"] = "FETCH_FAILED";
                response["message"] = "获取 SHIBOR 数据失败";
                res.status = 500;
                res.set_content(response.dump(), "application/json");
                return;
            }
            // 重新读取
            all_data.clear();
            cache_ok = ReadCsvData(all_data);
        }
        else if (!date.empty() && target_date > latest_date) {
            // 请求的日期比缓存新，需要拉取
            INFO("请求日期 {} 晚于缓存 {}，正在拉取", target_date, latest_date);
            bool fetch_ok = AppendFromPython(target_date);
            if (!fetch_ok) {
                response["status"] = "error";
                response["code"] = "FETCH_FAILED";
                response["message"] = "获取 SHIBOR 数据失败";
                res.status = 500;
                res.set_content(response.dump(), "application/json");
                return;
            }
            all_data.clear();
            cache_ok = ReadCsvData(all_data);
        }
        
        if (!cache_ok) {
            response["status"] = "error";
            response["code"] = "NO_DATA";
            response["message"] = "无 SHIBOR 数据";
            res.status = 404;
            res.set_content(response.dump(), "application/json");
            return;
        }
        
        // 按日期过滤
        nlohmann::json filtered = all_data;
        if (!target_date.empty()) {
            filtered = FilterByDate(all_data, target_date);
        }
        
        // 按期限过滤
        if (!term.empty()) {
            filtered = FilterByTerm(filtered, term);
        }
        
        // 响应
        response["status"] = "success";
        response["date"] = target_date;
        response["count"] = filtered.size();
        response["data"] = filtered;
        
        res.status = 200;
        res.set_content(response.dump(), "application/json");
    }
    catch (const std::exception& e) {
        WARN("SHIBOR 请求处理异常: {}", e.what());
        response["status"] = "error";
        response["message"] = e.what();
        res.status = 500;
        res.set_content(response.dump(), "application/json");
    }
}
