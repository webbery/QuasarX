#include <chrono>
#include "Util/datetime.h"
#include "Util/string_algorithm.h"
#include <cstdio>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <string.h>
#include <string>
#include "Util/system.h"

time_t FromStr(const std::string& dateString, const char* fmt) {
    // 初始化一个 tm 结构体来存储解析后的日期
    std::tm tm = {};
    // 创建一个字符串流来解析日期字符串
    std::istringstream ss(dateString);
    // 使用 std::get_time 解析日期字符串
    ss >> std::get_time(&tm, fmt);
    // 检查解析是否成功
    if (ss.fail()) {
        printf("Date parsing failed!\n");
        return -1;
    }
    // 将解析后的日期转换为 time_t 值
    std::time_t date = std::mktime(&tm);
    return date;
}

time_t FromTick(const std::string& str) {
    return atoll(str.c_str());
}

std::string ToString(time_t t, const char* fmt) {
    // 将 time_t 转换为 tm 结构
    std::tm *ltm = localtime(&t);
    
    // 创建一个字符数组来存储格式化后的日期字符串
    char buffer[80] = {0};
    
    // 使用 strftime 格式化日期
    strftime(buffer, sizeof(buffer), fmt, ltm);
    return std::string(buffer, strlen(buffer));
}

time_t Now()
{
  auto now = std::chrono::system_clock::now();
  auto utc_time = std::chrono::system_clock::to_time_t(now);

  std::tm local_tm = *std::localtime(&utc_time);
  return std::mktime(&local_tm);
}

// std::chrono::time_point<std::chrono::system_clock> FromLocalTime(time_t t) {
//     std::tm local_tm = *std::localtime(&t);
//     return std::chrono::system_clock::from_time_t(std::mktime(&local_tm));
// }

float Hour(const std::string& time) {
    float value = -1;
    Vector<String> nums;
    split(time, nums, ":");
    if (nums.size() == 2) {
        value = atoi(nums[0].c_str());
        value += atoi(nums[1].c_str())/60.0;
    }
    return value;
}

bool IsInTimeRange(time_t tick, char start_hour, char end_hour, char start_min, char end_min) {
    using namespace std::chrono;
    // 获取当前本地时间
    auto now = zoned_time{current_zone(), system_clock::from_time_t(tick)}.get_local_time();
    
    // 计算自当日午夜的持续时间
    auto midnight = floor<days>(now);
    auto since_midnight = now - midnight;
    
    // 定义时间范围
    auto start = hours{start_hour} + minutes{start_min};
    auto end = hours{end_hour} + minutes{end_min};
    
    return since_midnight >= start && since_midnight <= end;
}

time_range::time_range(int start_hour, int start_min, int start_sec, int end_hour, int end_min, int end_sec) {
    _start = ValidateAndConvert(start_hour, start_min, start_sec);
    _end = ValidateAndConvert(end_hour, end_min, end_sec);
}

time_range::time_range(int start_hour, int start_min, int end_hour, int end_min)
:time_range(start_hour, start_min, 0, end_hour, end_min, 0) {

}

fixed_time_range::fixed_time_range(const String& start, const String& end) {
    std::tm tm = {};
    std::istringstream ssStart(start);
    ssStart >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ssStart.fail()) {
        perror("Date parsing failed!\n");
        return ;
    }
    _start = std::mktime(&tm);
    std::istringstream ssEnd(end);
    ssEnd >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ssEnd.fail()) {
        perror("Date parsing failed!\n");
        return ;
    }
    _end = std::mktime(&tm);
}

fixed_time_range::fixed_time_range(const String& date) {
    std::tm tm = {};
    std::istringstream ss(date);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) {
        perror("Date parsing failed!\n");
        return ;
    }
    _start = std::mktime(&tm);
    
    tm.tm_hour +=23;
    tm.tm_min += 59;
    tm.tm_sec += 59;
    _end = std::mktime(&tm);
}


time_range::time_range():_start(-1), _end(-1) {}

int time_range::ValidateAndConvert(int hour, int min, int sec) {
    if (hour < 0 || hour > 23) throw std::invalid_argument("小时必须在0-23之间");
    if (min < 0 || min > 59) throw std::invalid_argument("分钟必须在0-59之间");
    if (sec < 0 || sec > 59) throw std::invalid_argument("秒必须在0-59之间");
    return hour * 3600 + min * 60 + sec;
}

bool operator == (const time_range& o1, const time_range& o2) {
    return o1.Start() == o2.Start() && o1.End() == o2.End();
}

bool operator == (const fixed_time_range& o1, const fixed_time_range& o2) {
    return o1.Start() == o2.Start() && o1.End() == o2.End();
}

bool time_range::operator == (time_t other) {
    return equal(other);
}

bool time_range::operator == (time_t other) const {
    return equal(other);
}

bool time_range::operator < (const time_range& other) const {
    return _end < other._start;
}

bool time_range::near_end(time_t t, int thresold) {
    struct tm *timeinfo = localtime(&t);
    if (!timeinfo) return false; // 时间转换失败

    int end_time = _end - 60 * thresold;
    int current_sec = timeinfo->tm_hour * 3600 + 
                        timeinfo->tm_min * 60 + 
                        timeinfo->tm_sec;
    if (_start > _end) { // TODO:
        return current_sec > end_time;
    } else { [[likely]]
        return current_sec > end_time;
    }
}

bool time_range::equal(time_t t) const {
    struct tm *timeinfo = localtime(&t);
    if (!timeinfo) return false; // 时间转换失败

    int current_sec = timeinfo->tm_hour * 3600 + 
                        timeinfo->tm_min * 60 + 
                        timeinfo->tm_sec;

    // 处理跨午夜时间段（如23:00-06:00）
    if (_start > _end) {
        return current_sec >= _start || current_sec <= _end;
    } 
    // 常规时间段
    else { [[likely]]
        return current_sec >= _start && current_sec <= _end;
    }
}

bool fixed_time_range::equal(time_t t) const {
    return t > _start && t < _end;
}

bool fixed_time_range::operator > (time_t other) const {
    return _start > other;
}

bool fixed_time_range::operator == (time_t other) const {
    return equal(other);
}

bool fixed_time_range::operator < (time_t other) const {
    return _end < other;
}

String fixed_time_range::DateTime() const {
    auto start = localtime(&_start);
    auto end = localtime(&_end);
    String ret;
    if (start->tm_year == end->tm_year) {
        ret += std::to_string(start->tm_year);
    }
    if (start->tm_mon == end->tm_mon) {
        ret += "-" + std::to_string(start->tm_mon);
    }
    if (start->tm_mday == end->tm_mday) {
        ret += "-" + std::to_string(start->tm_mday);
    }
    if (start->tm_hour == end->tm_hour) {
        ret += " " + std::to_string(start->tm_mday);
    }
    if (start->tm_min == end->tm_min) {
        ret += ":" + std::to_string(start->tm_min);
    }
    if (start->tm_sec == end->tm_sec) {
        ret += ":" + std::to_string(start->tm_sec);
    }
    return ret;
}

String to_string(const fixed_time_range& tr) {
    auto start = tr.Start();
    auto end = tr.End();
    std::tm *ltm = localtime(&start);
    return std::to_string(ltm->tm_year + 1900) + "-" + std::to_string(ltm->tm_mon + 1) + "-" + std::to_string(ltm->tm_mday);
}

Set<time_range> GetWorkingRange(ExchangeName name) {
    Set<time_range> result;
    switch (name) {
    case MT_Shenzhen: case MT_Shanghai: case MT_Beijing:
        result.emplace(time_range{9, 30, 11, 30});
        result.emplace(time_range{13, 00, 15, 00});
    break;
    default:
        WARN("not support time range for {}", (int)name);
    break;
    }
    return result;
}
