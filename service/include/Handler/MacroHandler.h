#pragma once
#include "HttpHandler.h"
#include "std_header.h"

#define API_MACRO "/market/macro"

/**
 * @brief 宏观经济数据 Handler
 *
 * 路由: GET /v0/market/macro
 *
 * 参数:
 *   - country:    china | usa | global (必填)
 *   - indicator:  cpi | ppi | gdp | pmi | m2 | social_financing 等 (必填)
 *                 FRED 指标: sofr | treasury_yield | treasury_spread | fx_rate
 *   - start:      开始日期 YYYY-MM-DD (可选)
 *   - end:        结束日期 YYYY-MM-DD (可选)
 *   - refresh:    true | false (可选，默认false)
 *
 * 数据源:
 *   - akshare 指标: 调用 tools/fetch_macro_data.py
 *   - FRED 指标:    调用 tools/fetch_fred_data.py (需 config.json 配置 fred_key)
 *
 * 缓存:
 *   {db_path}/macro/{country}/{indicator}.json
 *   过期时间: 7天 (akshare) / 1天 (FRED, 日频数据每日更新)
 *
 * 响应:
 *   {
 *     "status": "success",
 *     "country": "usa",
 *     "indicator": "sofr",
 *     "name": "SOFR 担保隔夜融资利率",
 *     "cached": true,
 *     "count": 252,
 *     "data": [{"date": "2024-01-02", "value": 5.32}, ...]
 *   }
 *
 *   多 series 指标 (如 treasury_yield) 的 data 中每条记录包含 series_id:
 *   "data": [{"date": "2024-01-02", "series_id": "DGS2", "value": 4.32}, ...]
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
    bool IsCacheExpired(const nlohmann::json& cached, bool is_fred = false);

    // === Python调用 ===
    bool FetchFromPython(const String& country, const String& indicator,
                         const String& start, const String& end, nlohmann::json& out);

    // === FRED API 调用 ===
    bool FetchFromFred(const String& indicator,
                       const String& start, const String& end,
                       nlohmann::json& out,
                       const String& api_key = "");  // 可选：从请求参数传递的 API Key
    bool IsFredIndicator(const String& indicator) const;
    std::vector<String> GetFredSeriesIds(const String& indicator) const;

    // === 工具函数 ===
    nlohmann::json FilterByDate(const nlohmann::json& data, const String& start, const String& end);
    String GetParam(const httplib::Request& req, const String& key, const String& default_val = "");

    // 缓存过期时间（秒）：7天
    static constexpr int CACHE_TTL_SECONDS = 7 * 24 * 3600;
    // FRED 缓存过期时间（秒）：1天 (日频数据)
    static constexpr int FRED_CACHE_TTL_SECONDS = 24 * 3600;

    // 指标白名单
    static const Set<String> VALID_INDICATORS;
    static const Set<String> VALID_COUNTRIES;

    // FRED 指标 → series ID 映射
    static const Map<String, std::vector<String>> FRED_INDICATOR_MAP;
    // FRED 指标 → 显示名称
    static const Map<String, String> FRED_INDICATOR_NAMES;
};
