#include "Util/HolidayCalendar.h"
#include "Util/log.h"
#include "json.hpp"
#include "httplib.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include "Util/datetime.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kBaseUrl = "https://raw.githubusercontent.com/NateScarlet/holiday-cn/master/";

/// 构造 "YYYY-MM-DD" 字符串
String formatYmd(int year, int month, int day) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    return String(buf);
}

/// 解析 "YYYY-MM-DD"，失败返回 -1
int parseYear(const String& ymd) {
    if (ymd.size() < 4) return -1;
    try {
        return std::stoi(ymd.substr(0, 4));
    } catch (...) {
        return -1;
    }
}

}  // namespace

HolidayCalendar& HolidayCalendar::instance() {
    static HolidayCalendar inst;
    return inst;
}

String HolidayCalendar::defaultCacheDir(const String& databasePath) {
    if (databasePath.empty()) return "data/holidays";
    if (databasePath.back() == '/' || databasePath.back() == '\\') {
        return databasePath + "holidays";
    }
    return databasePath + "/holidays";
}

void HolidayCalendar::loadFromCache(const String& cacheDir) {
    _cacheDir = cacheDir;
    std::error_code ec;
    fs::create_directories(cacheDir, ec);
    if (ec) {
        WARN("[HolidayCalendar] Failed to create cache dir {}: {}", cacheDir, ec.message());
        return;
    }

    int loaded = 0;
    for (auto& entry : fs::directory_iterator(cacheDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        if (path.extension() != ".json") continue;
        int year = parseYear(path.stem().string());
        if (year < 0) continue;

        std::ifstream ifs(path.string());
        if (!ifs.is_open()) {
            WARN("[HolidayCalendar] Cannot open cache file: {}", path.string());
            continue;
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        if (!parseJson(year, ss.str())) {
            WARN("[HolidayCalendar] Failed to parse cache: {}", path.string());
            continue;
        }
        ++loaded;
    }

    {
        std::unique_lock lock(_mtx);
        _meta.cachedYears = static_cast<int>(_holidays.size());
    }
    INFO("[HolidayCalendar] Loaded {} year(s) from cache: {}",
         loaded, cacheDir);
}

bool HolidayCalendar::fetchAndCache(int year) {
    String url = String(kBaseUrl) + std::to_string(year) + ".json";

    httplib::Client cli("https://raw.githubusercontent.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(15);
    cli.set_follow_location(true);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#ifndef _WIN32
    const char* ca_path = "/etc/ssl/certs/ca-certificates.crt";
    if (fs::exists(ca_path)) {
        cli.set_ca_cert_path(ca_path);
    }
#endif
#endif

    auto res = cli.Get(url.c_str());
    bool onlineOk = res && res->status == 200;

    if (!onlineOk) {
        int status = res ? res->status : -1;
        std::unique_lock lock(_mtx);
        _meta.onlineSuccess = false;
        WARN("[HolidayCalendar] Fetch {} failed (http={}), keeping in-memory data",
             year, status);
        return false;
    }

    if (!parseJson(year, res->body)) {
        std::unique_lock lock(_mtx);
        _meta.onlineSuccess = false;
        WARN("[HolidayCalendar] Fetch {} succeeded but JSON parse failed", year);
        return false;
    }

    {
        std::unique_lock lock(_mtx);
        _meta.lastFetchYear = year;
        _meta.lastUpdate = std::time(nullptr);
        _meta.onlineSuccess = true;
        _meta.cachedYears = static_cast<int>(_holidays.size());
    }

    if (!_cacheDir.empty()) {
        if (!writeCacheFile(_cacheDir, year)) {
            WARN("[HolidayCalendar] Fetched year {} but failed to write cache", year);
        }
    }

    INFO("[HolidayCalendar] Fetched year {} ({} holidays) from {}",
         year, _holidays.count(year) ? _holidays.at(year).size() : 0, url);
    return true;
}

bool HolidayCalendar::ensureCurrentYearLoaded(int year) {
    bool ok = fetchAndCache(year);
    if (ok) {
        INFO("[HolidayCalendar] Current year {} ready", year);
    } else {
        WARN("[HolidayCalendar] Current year {} fetch failed; using cache only", year);
    }
    return ok;
}

bool HolidayCalendar::ensureYearLoaded(int year) {
    bool ok = fetchAndCache(year);
    if (ok) {
        INFO("[HolidayCalendar] Annual refresh: year {} ready", year);
    } else {
        WARN("[HolidayCalendar] Annual refresh failed for year {}", year);
    }
    return ok;
}

Vector<String> HolidayCalendar::getHolidaysForYear(int year) const {
    std::shared_lock lock(_mtx);
    auto it = _holidays.find(year);
    if (it == _holidays.end()) return {};
    return Vector<String>(it->second.begin(), it->second.end());
}

bool HolidayCalendar::isHoliday(int year, int month, int day) const {
    return isHoliday(formatYmd(year, month, day));
}

bool HolidayCalendar::isHoliday(const String& ymd) const {
    int year = parseYear(ymd);
    if (year < 0) return false;
    std::shared_lock lock(_mtx);
    auto it = _holidays.find(year);
    if (it == _holidays.end()) return false;
    return it->second.count(ymd) > 0;
}

HolidayCalendar::Meta HolidayCalendar::getMeta() const {
    std::shared_lock lock(_mtx);
    return _meta;
}

bool HolidayCalendar::parseJson(int year, const String& jsonText) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonText);
    } catch (const std::exception& e) {
        return false;
    }

    if (!j.contains("days") || !j["days"].is_array()) return false;

    Set<String> days;
    for (auto& d : j["days"]) {
        // 只取 isOffDay=true 的假日；isOffDay=false 是调休补班日，A 股正常开市
        if (!d.contains("isOffDay") || !d["isOffDay"].is_boolean() || !d["isOffDay"].get<bool>()) {
            continue;
        }
        if (!d.contains("date") || !d["date"].is_string()) continue;
        String date = d["date"].get<String>();
        if (parseYear(date) != year) continue;  // 防御 year 字段不一致
        days.insert(std::move(date));
    }

    if (days.empty()) return false;  // 空集合视为解析失败（保留旧数据）

    {
        std::unique_lock lock(_mtx);
        _holidays[year] = std::move(days);
    }
    return true;
}

bool HolidayCalendar::writeCacheFile(const String& cacheDir, int year) const {
    std::shared_lock lock(_mtx);
    auto it = _holidays.find(year);
    if (it == _holidays.end()) return false;

    fs::create_directories(cacheDir);
    String path = cacheDir + "/" + std::to_string(year) + ".json";

    nlohmann::json j;
    j["year"] = year;
    j["days"] = nlohmann::json::array();
    for (auto& d : it->second) {
        j["days"].push_back({{"date", d}, {"isOffDay", true}});
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << j.dump(2);
    return ofs.good();
}
