#include "Handler/MacroHandler.h"
#include "Util/log.h"
#include "Util/system.h"
#include "server.h"
#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;

// === 指标白名单 ===
const Set<String> MacroHandler::VALID_INDICATORS = {
    "cpi", "ppi", "gdp", "pmi", "m2", "social_financing",
    "unemployment", "trade", "interest_rate", "retail_sales",
    "industrial_production", "fixed_asset_investment",
    "consumer_confidence", "housing_starts", "nonfarm"
};

const Set<String> MacroHandler::VALID_COUNTRIES = {
    "china", "usa", "global"
};

MacroHandler::MacroHandler(Server* server) : HttpHandler(server) {
}

// ============================================================
// 缓存相关
// ============================================================

String MacroHandler::GetCachePath(const String& country, const String& indicator) {
    auto db_path = _server->GetConfig().GetDatabasePath();
    return db_path + "/macro/" + country + "/" + indicator + ".json";
}

bool MacroHandler::ReadCache(const String& country, const String& indicator, nlohmann::json& out) {
    String path = GetCachePath(country, indicator);
    if (!fs::exists(path)) {
        return false;
    }

    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            return false;
        }
        ifs >> out;
        return out.contains("data") && out["data"].is_array();
    }
    catch (const std::exception& e) {
        WARN("[MacroHandler] 读取缓存失败: {}", e.what());
        return false;
    }
}

bool MacroHandler::SaveCache(const String& country, const String& indicator, const nlohmann::json& data) {
    String path = GetCachePath(country, indicator);
    String dir = fs::path(path).parent_path().string();

    try {
        fs::create_directories(dir);
        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            WARN("[MacroHandler] 无法写入缓存: {}", path);
            return false;
        }
        ofs << data.dump(2);
        ofs.close();
        INFO("[MacroHandler] 缓存已保存: {}", path);
        return true;
    }
    catch (const std::exception& e) {
        WARN("[MacroHandler] 保存缓存失败: {}", e.what());
        return false;
    }
}

bool MacroHandler::IsCacheExpired(const nlohmann::json& cached) {
    if (!cached.contains("updated_at")) {
        return true;  // 没有时间戳，视为过期
    }

    String updated_at = cached["updated_at"];
    // 解析 ISO 8601: "2026-06-02T13:30:00"
    struct tm tm_val = {};
    if (sscanf(updated_at.c_str(), "%d-%d-%dT%d:%d:%d",
               &tm_val.tm_year, &tm_val.tm_mon, &tm_val.tm_mday,
               &tm_val.tm_hour, &tm_val.tm_min, &tm_val.tm_sec) != 6) {
        return true;  // 解析失败，视为过期
    }
    tm_val.tm_year -= 1900;
    tm_val.tm_mon -= 1;
    time_t cache_time = mktime(&tm_val);

    auto now = std::chrono::system_clock::now();
    time_t now_t = std::chrono::system_clock::to_time_t(now);

    return (now_t - cache_time) > CACHE_TTL_SECONDS;
}

// ============================================================
// Python调用
// ============================================================

bool MacroHandler::FetchFromPython(const String& country, const String& indicator,
                                    const String& start, const String& end, nlohmann::json& out) {
    String cmd = fmt::format("python3 tools/fetch_macro_data.py --indicator {} --country {} --json",
                            indicator, country);
    if (!start.empty()) {
        cmd += fmt::format(" --start {}", start);
    }
    if (!end.empty()) {
        cmd += fmt::format(" --end {}", end);
    }

    String output;
    bool success = RunCommand(cmd, output);

    if (!success || output.empty()) {
        WARN("[MacroHandler] Python脚本执行失败: {}", cmd);
        return false;
    }

    try {
        // 解析JSON输出
        out = nlohmann::json::parse(output);
        return out.contains("data") && out["data"].is_array();
    }
    catch (const std::exception& e) {
        WARN("[MacroHandler] 解析Python输出失败: {}, output: {}", e.what(), output.substr(0, 200));
        return false;
    }
}

// ============================================================
// 工具函数
// ============================================================

nlohmann::json MacroHandler::FilterByDate(const nlohmann::json& data, const String& start, const String& end) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& item : data) {
        String d = item.value("date", "");
        if (d.empty()) continue;

        bool match = true;
        if (!start.empty() && d < start) match = false;
        if (!end.empty() && d > end) match = false;

        if (match) {
            result.push_back(item);
        }
    }
    return result;
}

String MacroHandler::GetParam(const httplib::Request& req, const String& key, const String& default_val) {
    auto it = req.params.find(key);
    return it != req.params.end() ? it->second : default_val;
}

// ============================================================
// GET /v0/macro
// ============================================================

void MacroHandler::get(const httplib::Request& req, httplib::Response& res) {
    String country = GetParam(req, "country");
    String indicator = GetParam(req, "indicator");
    String start = GetParam(req, "start");
    String end = GetParam(req, "end");
    String refresh_str = GetParam(req, "refresh", "false");
    bool refresh = (refresh_str == "true");

    nlohmann::json response;

    // 参数校验
    if (country.empty() || indicator.empty()) {
        response["status"] = "error";
        response["code"] = "MISSING_PARAMS";
        response["message"] = "缺少必填参数: country, indicator";
        res.status = 400;
        res.set_content(response.dump(), "application/json");
        return;
    }

    if (!VALID_COUNTRIES.count(country)) {
        response["status"] = "error";
        response["code"] = "INVALID_COUNTRY";
        response["message"] = fmt::format("无效的国家参数: {}，支持: {}", country,
                                          fmt::join(VALID_COUNTRIES, ", "));
        res.status = 400;
        res.set_content(response.dump(), "application/json");
        return;
    }

    if (!VALID_INDICATORS.count(indicator)) {
        response["status"] = "error";
        response["code"] = "INVALID_INDICATOR";
        response["message"] = fmt::format("无效的指标参数: {}，支持: {}", indicator,
                                          fmt::join(VALID_INDICATORS, ", "));
        res.status = 400;
        res.set_content(response.dump(), "application/json");
        return;
    }

    try {
        nlohmann::json cached_data;
        bool cache_ok = ReadCache(country, indicator, cached_data);
        bool cache_expired = !cache_ok || IsCacheExpired(cached_data);

        bool use_cache = cache_ok && !cache_expired && !refresh;

        if (use_cache) {
            // 使用缓存
            INFO("[MacroHandler] 使用缓存: {}/{}", country, indicator);
            response["cached"] = true;
            response["stale"] = false;
            response["data"] = cached_data["data"];
            response["name"] = cached_data.value("name", "");
            response["unit"] = cached_data.value("unit", "");
        }
        else {
            // 调用Python获取新数据
            INFO("[MacroHandler] 获取新数据: {}/{} (缓存{}{})",
                 country, indicator,
                 cache_ok ? "存在但" : "不存在",
                 cache_expired ? "已过期" : "");

            nlohmann::json fresh_data;
            bool fetch_ok = FetchFromPython(country, indicator, start, end, fresh_data);

            if (fetch_ok) {
                // 保存缓存
                SaveCache(country, indicator, fresh_data);
                response["cached"] = false;
                response["stale"] = false;
                response["data"] = fresh_data["data"];
                response["name"] = fresh_data.value("name", "");
                response["unit"] = fresh_data.value("unit", "");
            }
            else if (cache_ok) {
                // Python失败但有旧缓存，降级返回旧数据
                WARN("[MacroHandler] 获取新数据失败，降级使用旧缓存");
                response["cached"] = true;
                response["stale"] = true;
                response["data"] = cached_data["data"];
                response["name"] = cached_data.value("name", "");
                response["unit"] = cached_data.value("unit", "");
            }
            else {
                // 无缓存且Python失败
                response["status"] = "error";
                response["code"] = "FETCH_FAILED";
                response["message"] = "获取宏观数据失败";
                res.status = 500;
                res.set_content(response.dump(), "application/json");
                return;
            }
        }

        // 按日期过滤
        nlohmann::json& data = response["data"];
        if (!start.empty() || !end.empty()) {
            data = FilterByDate(data, start, end);
        }

        // 最终响应
        response["status"] = "success";
        response["country"] = country;
        response["indicator"] = indicator;
        response["count"] = data.size();

        res.status = 200;
        res.set_content(response.dump(2), "application/json");
    }
    catch (const std::exception& e) {
        WARN("[MacroHandler] 处理异常: {}", e.what());
        response["status"] = "error";
        response["message"] = e.what();
        res.status = 500;
        res.set_content(response.dump(), "application/json");
    }
}
