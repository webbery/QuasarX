#pragma once
#include "std_header.h"
#include <chrono>
#include <utility>

time_t FromStr(const String& str, const char* fmt = "%Y-%m-%d");
time_t FromTick(const String& str);

/**
 * 按壁钟分量解析 "YYYY-MM-DD[ HH:MM:SS]" 为 epoch 微秒（naive，无时区转换）。
 * 与 DuckDB VARCHAR→TIMESTAMP 解析语义一致；FromStr 的 mktime 会引入本地时区偏移
 */
int64_t FromNaiveTimestamp(const String& str);

std::string ToString(time_t, const char* fmt = "%Y-%m-%d %H:%M:%S");

time_t Now();

/**
 * 将时间戳截断到日期（本地时区，抹去时分秒）
 * 用于跨日检测等只关心 y/m/d 的场合
 */
time_t DateOnly(time_t ts);

/**
 * ISO 8601 周年周号，返回 iso_year * 100 + iso_week
 *
 * 周频聚合的边界判定：同一 (iso_year, iso_week) 即同一周。
 * 口径对齐 lab/ahhmm/panel.py 的 df.dt.isocalendar()。
 *
 * 注意：ISO 周从周一开始，且跨年时 iso_week 可能重号（如 2020-12-31 属
 * 2020-W53，2021-01-01 属 2021-W01），故必须用 (iso_year, iso_week) 整体
 * 比较，不能只看周号。
 */
int IsoWeekId(time_t ts);

/**
 * 取时间戳所属 ISO 周的周一零点，作为周频 bar 的起始时间
 *
 * 同一 ISO 周内的所有交易日返回同一值，故可用它判断「是否进入新的一周」。
 * 跨年时 IsoWeekId 可能重号，但周一起点始终递增，按 barStart 比较仍然安全。
 */
time_t IsoWeekMonday(time_t ts);

// 定义在 KBarBuilder.h；此处仅前置声明以避免与 Bridge/exchange.h 的循环包含
enum class BarFreq : char;

/**
 * bar 频率对应的固定秒数
 *
 * 周频取 604800（ISO 周恒为 7 天），但周频的实际边界由 IsoWeekMonday 决定，
 * 不能用「起始时间 + 本函数」的方式自行推算。
 *
 * 例: BarFreq::Day → 86400, BarFreq::Min1 → 60, BarFreq::Week → 604800
 */
int64_t BarFreqToSeconds(BarFreq freq);

/**
 * 解析带后缀的时间字符串，返回数值和单位
 * 支持后缀: s(秒), m(分), h(时), d(日)，无后缀视为裸数字
 * 例: "5d" → {5, 'd'}, "30m" → {30, 'm'}, "1h" → {1, 'h'}, "10" → {10, '\0'}
 */
struct TimeValue {
    int value;
    char unit;  // 's', 'm', 'h', 'd', '\0'
};
TimeValue ParseTimeValue(const std::string& str);

/**
 * 将任意 "Ns/Nm/Nh/Nd" 时间字符串换算为秒数（壁钟：1d = 86400s）。
 * 解析失败返回 -1。
 * 例: "5d" → 432000, "1h" → 3600, "30s" → 30, "20d" → 1728000。
 */
int TimeStringToSeconds(const std::string& str);

/**
 * 将任意 "Ns/Nm/Nh/Nd" 时间字符串换算为分钟数（交易日：1d = 240 min）。
 * 解析失败返回 -1。与 TimeStringToSeconds 区别在于 1d 的换算基准
 * （交易日 4 小时 vs 壁钟 24 小时），与 FunctionNode::TimeValueToBars 保持一致。
 * 例: "5d" → 1200, "1h" → 60, "30s" → 1, "20d" → 4800。
 */
int TimeStringToMinutes(const std::string& str);

float Hour(const String& time);

bool IsInTimeRange(time_t tick, char start_hour, char end_hour, char start_min, char end_min);

std::chrono::time_point<std::chrono::system_clock> FromLocalTime(time_t t);

class time_range {
public:
    time_range();
    time_range(const time_range&) = default;
    time_range(int start_hour, int start_min, int start_sec, int end_hour, int end_min, int end_sec);
    time_range(int start_hour, int start_min, int end_hour, int end_min);


    bool operator == (time_t);
    bool operator == (time_t) const;

    bool operator < (const time_range&) const;

    int Start() const {return _start;}
    int End() const {return _end;}

    bool near_end(time_t t, int thresold = 30);
private:
    int ValidateAndConvert(int hour, int min, int sec);
    
    bool equal(time_t) const;
private:

    int _start;
    int _end;
};

class fixed_time_range {
public:
    fixed_time_range() = default;
    fixed_time_range(const fixed_time_range&) = default;
    
    fixed_time_range(const String& start, const String& end);
    fixed_time_range(const String& date);
    // 返回首尾时间相同的部分
    String DateTime() const ;

    time_t Start() const { return _start; }
    time_t End() const { return _end; }

    bool IsDaily() const;

    bool operator == (time_t) const;
    bool operator < (time_t) const;
    bool operator > (time_t) const;
private:
    bool equal(time_t) const;

private:
    time_t _start;
    time_t _end;
};

namespace std {
    template<>
    struct hash<time_range> {
        size_t operator()(const time_range& tr) const {
            size_t h1 = std::hash<int>{}(tr.Start());
            size_t h2 = std::hash<int>{}(tr.End());
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    template<>
    struct hash<fixed_time_range> {
        size_t operator()(const fixed_time_range& tr) const {
            size_t h1 = std::hash<time_t>{}(tr.Start());
            size_t h2 = std::hash<time_t>{}(tr.End());
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };
}

String to_string(const fixed_time_range& tr);

bool operator == (const time_range&, const time_range&);
bool operator == (const fixed_time_range&, const fixed_time_range&);


enum ExchangeName: char;
Set<time_range> GetWorkingRange(ExchangeName);
