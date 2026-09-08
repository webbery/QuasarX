#pragma once

#include "std_header.h"
#include <shared_mutex>
#include <ctime>

/**
 * @brief 中国法定假日 / A 股交易日历（中央单例）
 *
 * 数据源：https://github.com/NateScarlet/holiday-cn
 * 数据格式：
 *   {
 *     "year": 2025,
 *     "days": [
 *       {"name": "元旦", "date": "2025-01-01", "isOffDay": true},
 *       {"name": "春节", "date": "2025-01-26", "isOffDay": false},  // 调休补班日
 *       ...
 *     ]
 *   }
 *
 * 注意：只取 isOffDay=true 的日期；isOffDay=false 是调休补班日，A 股正常开市。
 *
 * 加载策略：
 *   - 启动时 Server::Init() 调用 loadFromCache(dir) 读本地缓存（避免冷启动失败）
 *   - 随后 fetchAndCache(currentYear) 从 GitHub 拉取覆盖
 *   - 每年 12 月 20 日定时器调用 fetchAndCache(currentYear+1) 提前一年
 *   - 网络失败时保留旧内存数据，WARN 不 FATAL
 *
 * 缓存路径：{database_path}/holidays/{year}.json
 *
 * 线程模型：std::shared_mutex，读多写少
 */
class HolidayCalendar {
public:
    static HolidayCalendar& instance();

    /// 从本地缓存目录加载所有已存在的 {year}.json
    /// 启动时优先调用，避免冷启动受网络影响
    void loadFromCache(const String& cacheDir);

    /// 从 GitHub raw 拉取指定年份数据，成功后写本地缓存
    /// 失败时返回 false，内存数据保持不变
    bool fetchAndCache(int year);

    /// 首次启动：拉取当前年份（包装 fetchAndCache + 友好日志）
    bool ensureCurrentYearLoaded(int year);

    /// 年度刷新：拉取指定年份（包装 fetchAndCache + 友好日志）
    bool ensureYearLoaded(int year);

    /// 返回指定年份的假日列表（"YYYY-MM-DD" 字符串，已排序）
    Vector<String> getHolidaysForYear(int year) const;

    /// 判断指定自然日是否为假日
    bool isHoliday(int year, int month, int day) const;

    /// 判断 "YYYY-MM-DD" 字符串是否为假日
    bool isHoliday(const String& ymd) const;

    /// 缓存目录路径（默认 {database_path}/holidays）
    static String defaultCacheDir(const String& databasePath);

    /// 元信息（用于日志/调试）
    struct Meta {
        int lastFetchYear = 0;       // 最近一次 fetch 的年份
        time_t lastUpdate = 0;       // 最近一次成功 fetch 的时间戳
        bool onlineSuccess = false;  // 最近一次 fetch 是否网络成功
        int cachedYears = 0;         // 当前内存中缓存的年份数
    };
    Meta getMeta() const;

private:
    HolidayCalendar() = default;

    /// 解析 GitHub JSON 响应并写入 _holidays[year]
    /// 返回 true 表示解析成功（可能 0 天，全部放假都视为成功）
    bool parseJson(int year, const String& jsonText);

    /// 把内存中指定年份的 holidays 写到 {cacheDir}/{year}.json
    bool writeCacheFile(const String& cacheDir, int year) const;

    mutable std::shared_mutex _mtx;
    Map<int, Set<String>> _holidays;
    Meta _meta;
};
