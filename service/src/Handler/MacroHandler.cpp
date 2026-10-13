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
    // akshare 指标
    "cpi", "ppi", "gdp", "pmi", "m2", "social_financing",
    "unemployment", "trade", "interest_rate", "retail_sales",
    "industrial_production", "fixed_asset_investment",
    "consumer_confidence", "housing_starts", "nonfarm",
    // FRED 指标
    "sofr", "treasury_yield", "treasury_spread", "fx_rate",
};

const Set<String> MacroHandler::VALID_COUNTRIES = {
    "china", "usa", "global"
};

// === FRED 指标 → series ID 映射 ===
const Map<String, std::vector<String>> MacroHandler::FRED_INDICATOR_MAP = {
    {"sofr",            {"SOFR"}},
    {"treasury_yield",  {"DGS2", "DGS5", "DGS10", "DGS30"}},
    {"treasury_spread", {"T10Y2Y", "T10Y3M"}},
    {"fx_rate",         {"DEXCHUS", "DEXUSEU", "DEXJPUS", "DEXHKUS", "DEXUSUK", "DEXUSAL", "DEXUSCA"}},
};

// === FRED 指标 → 显示名称 ===
const Map<String, String> MacroHandler::FRED_INDICATOR_NAMES = {
    {"sofr",            "SOFR 担保隔夜融资利率"},
    {"treasury_yield",  "美国国债收益率 (2Y/5Y/10Y/30Y)"},
    {"treasury_spread", "美国国债利差 (10Y-2Y / 10Y-3M)"},
    {"fx_rate",         "主要货币兑美元汇率"},
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

bool MacroHandler::IsCacheExpired(const nlohmann::json& cached, bool is_fred) {
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

    // FRED 数据使用更短的缓存时间 (1天 vs 7天)
    int ttl = is_fred ? FRED_CACHE_TTL_SECONDS : CACHE_TTL_SECONDS;
    return (now_t - cache_time) > ttl;
}

// ============================================================
// Python调用 (akshare)
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

    // 提取 JSON 部分：跳过 Windows cmd 的 "Active code page: 65001" 前缀
    auto json_start = output.find('{');
    if (json_start != std::string::npos) {
        output = output.substr(json_start);
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
// FRED API 调用
// ============================================================

bool MacroHandler::IsFredIndicator(const String& indicator) const {
    return FRED_INDICATOR_MAP.find(indicator) != FRED_INDICATOR_MAP.end();
}

std::vector<String> MacroHandler::GetFredSeriesIds(const String& indicator) const {
    auto it = FRED_INDICATOR_MAP.find(indicator);
    if (it != FRED_INDICATOR_MAP.end()) {
        return it->second;
    }
    return {};
}

bool MacroHandler::FetchFromFred(const String& indicator,
                                  const String& start, const String& end,
                                  nlohmann::json& out,
                                  const String& api_key) {
    // 优先使用从请求参数传递的 API Key，否则从配置文件获取
    String fred_key = api_key;
    if (fred_key.empty()) {
        fred_key = _server->GetConfig().GetFredKey();
    }
    
    if (fred_key.empty()) {
        WARN("[MacroHandler] FRED API Key 未配置, 请在 config.json 的 server.fred_key 中设置，或通过请求参数 fred_key 传递");
        return false;
    }

    // 获取该指标对应的所有 series ID
    auto series_ids = GetFredSeriesIds(indicator);
    if (series_ids.empty()) {
        WARN("[MacroHandler] 未知的 FRED 指标: {}", indicator);
        return false;
    }

    // 构建命令: python3 tools/fetch_fred_data.py --api-key KEY --series S1 S2 ... --json
    String cmd = fmt::format("python3 tools/fetch_fred_data.py --api-key {}", fred_key);
    cmd += " --series";
    for (const auto& sid : series_ids) {
        cmd += fmt::format(" {}", sid);
    }
    cmd += " --json";

    if (!start.empty()) {
        cmd += fmt::format(" --start {}", start);
    }
    if (!end.empty()) {
        cmd += fmt::format(" --end {}", end);
    }

    String output;
    bool success = RunCommand(cmd, output);

    if (!success || output.empty()) {
        WARN("[MacroHandler] FRED Python脚本执行失败: {}", cmd);
        return false;
    }

    // 提取 JSON 部分：跳过 Windows cmd 的 "Active code page: 65001" 前缀
    // 找到第一个 '{' 字符，从那里开始是纯 JSON
    auto json_start = output.find('{');
    if (json_start != std::string::npos) {
        output = output.substr(json_start);
    }

    try {
        // 解析 fetch_fred_data.py 的 JSON 输出
        // 格式: {"updated_at": "...", "series": [{"series_id": "...", "name": "...", "data": [...]}]}
        auto fred_output = nlohmann::json::parse(output);

        if (!fred_output.contains("series") || !fred_output["series"].is_array()) {
            WARN("[MacroHandler] FRED 输出格式错误: 缺少 series 字段");
            return false;
        }

        // 转换为 MacroHandler 标准格式
        // 单 series: data = [{date, value}, ...]
        // 多 series: data = [{date, series_id, value}, ...]
        nlohmann::json data_array = nlohmann::json::array();
        String combined_name;
        String combined_unit;

        const auto& series_array = fred_output["series"];
        bool multi_series = (series_array.size() > 1);

        for (const auto& s : series_array) {
            String sid = s.value("series_id", "");
            String sname = s.value("name", "");
            String sunit = s.value("unit", "");

            if (combined_name.empty()) {
                combined_name = sname;
                combined_unit = sunit;
            }

            if (!s.contains("data") || !s["data"].is_array()) continue;

            for (const auto& item : s["data"]) {
                nlohmann::json entry;
                entry["date"] = item.value("date", "");
                entry["value"] = item.value("value", 0.0);
                if (multi_series) {
                    entry["series_id"] = sid;
                }
                data_array.push_back(entry);
            }
        }

        // 构造标准输出
        auto name_it = FRED_INDICATOR_NAMES.find(indicator);
        String display_name = (name_it != FRED_INDICATOR_NAMES.end()) ? name_it->second : combined_name;

        out["name"] = display_name;
        out["unit"] = combined_unit;
        out["data"] = data_array;
        out["updated_at"] = fred_output.value("updated_at", "");

        return true;
    }
    catch (const std::exception& e) {
        WARN("[MacroHandler] 解析FRED输出失败: {}, output: {}", e.what(), output.substr(0, 200));
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
    String fred_key = GetParam(req, "fred_key");  // 从请求参数获取 FRED API Key（可选）
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
        bool is_fred = IsFredIndicator(indicator);

        nlohmann::json cached_data;
        bool cache_ok = ReadCache(country, indicator, cached_data);
        bool cache_expired = !cache_ok || IsCacheExpired(cached_data, is_fred);

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
            // 获取新数据
            INFO("[MacroHandler] 获取新数据: {}/{} (缓存{}{}, 数据源: {})",
                 country, indicator,
                 cache_ok ? "存在但" : "不存在",
                 cache_expired ? "已过期" : "",
                 is_fred ? "FRED" : "akshare");

            nlohmann::json fresh_data;
            bool fetch_ok = false;

            if (is_fred) {
                fetch_ok = FetchFromFred(indicator, start, end, fresh_data, fred_key);
            } else {
                fetch_ok = FetchFromPython(country, indicator, start, end, fresh_data);
            }

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
                // 获取失败但有旧缓存，降级返回旧数据
                WARN("[MacroHandler] 获取新数据失败，降级使用旧缓存");
                response["cached"] = true;
                response["stale"] = true;
                response["data"] = cached_data["data"];
                response["name"] = cached_data.value("name", "");
                response["unit"] = cached_data.value("unit", "");
            }
            else {
                // 无缓存且获取失败
                response["status"] = "error";
                response["code"] = "FETCH_FAILED";
                response["message"] = is_fred
                    ? fmt::format("获取FRED数据失败 (指标: {})", indicator)
                    : "获取宏观数据失败";
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
