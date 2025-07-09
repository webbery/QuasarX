#include <chrono>
#include "Util/datetime.h"
#include "Util/string_algorithm.h"
#include <sstream>
#include <ctime>
#include <iomanip>
#include <string.h>
#include "Util/system.h"
#include "Util/log.h"

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

std::string ToString(time_t t) {
    // 将 time_t 转换为 tm 结构
    std::tm *ltm = localtime(&t);
    
    // 创建一个字符数组来存储格式化后的日期字符串
    char buffer[80] = {0};
    
    // 使用 strftime 格式化日期
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", ltm);
    return std::string(buffer, strlen(buffer));
}

time_t Now()
{
  auto now = std::chrono::system_clock::now();
  return std::chrono::system_clock::to_time_t(now);
  /*std::string formatted_time = std::format("{%Y-%m-%d %H:%M:%S}", *std::localtime(&time_t_now));
  return formatted_time;*/
}

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

bool time_range::operator == (time_t other) {
    return equal(other);
}

bool time_range::operator == (time_t other) const {
    return equal(other);
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
UnorderedSet<time_range> GetWorkingRange(ExchangeName name) {
    UnorderedSet<time_range> result;
    switch (name) {
    case MT_Shenzhen: case MT_Shanghai: case MT_Beijing:
        result.emplace(time_range{9, 30, 11, 30});
        result.emplace(time_range{13, 00, 15, 00});
    break;
    default:
        WARN("not support time range for {}", name);
    break;
    }
    return result;
}
