#pragma once
#include "HttpHandler.h"
#include "std_header.h"

#define API_MACRO "/market/macro"

/**
 * @brief 宏观经济数据 Handler
 *
 * 路由: GET /v0/macro
 *
 * 参数:
 *   - country:    china | usa | global (必填)
 *   - indicator:  cpi | ppi | gdp | pmi | m2 | social_financing 等 (必填)
 *   - start:      开始日期 YYYY-MM-DD (可选)
 *   - end:        结束日期 YYYY-MM-DD (可选)
 *   - refresh:    true | false (可选，默认false)
 *
 * 缓存:
 *   {db_path}/macro/{country}/{indicator}.json
 *   过期时间: 7天
 *
 * 响应:
 *   {
 *     "status": "success",
 *     "country": "china",
 *     "indicator": "cpi",
 *     "name": "中国居民消费价格指数(CPI)",
 *     "cached": true,
 *     "count": 12,
 *     "data": [{"date": "2025-09-10", "value": 0.1}, ...]
 *   }
 */
class MacroHandler : public HttpHandler {
public:
    MacroHandler(Server* server);

    void get(const httplib::Request& req, httplib::Response& res) override;

private:
    // === 缓存相关 ===
    String GetCachePath(const String& country, const String& indicator);
    bool ReadCache(const String& country, const String& indicator, nlohmann::json& out);
    bool SaveCache(const String& country, const String& indicator, const nlohmann::json& data);
    bool IsCacheExpired(const nlohmann::json& cached);

    // === Python调用 ===
    bool FetchFromPython(const String& country, const String& indicator,
                         const String& start, const String& end, nlohmann::json& out);

    // === 工具函数 ===
    nlohmann::json FilterByDate(const nlohmann::json& data, const String& start, const String& end);
    String GetParam(const httplib::Request& req, const String& key, const String& default_val = "");

    // 缓存过期时间（秒）：7天
    static constexpr int CACHE_TTL_SECONDS = 7 * 24 * 3600;

    // 指标白名单
    static const Set<String> VALID_INDICATORS;
    static const Set<String> VALID_COUNTRIES;
};
