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
