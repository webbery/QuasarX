#pragma once
#include "std_header.h"
#include <utility>

time_t FromStr(const String& str, const char* fmt = "%Y-%m-%d");
time_t FromTick(const String& str);

std::string ToString(time_t);

time_t Now();

float Hour(const String& time);

bool IsInTimeRange(time_t tick, char start_hour, char end_hour, char start_min, char end_min);

class time_range {
public:
    time_range();
    time_range(const time_range&) = default;
    time_range(int start_hour, int start_min, int start_sec, int end_hour, int end_min, int end_sec);
    time_range(int start_hour, int start_min, int end_hour, int end_min);

    bool operator == (time_t);
    bool operator == (time_t) const;

    int Start() const {return _start;}
    int End() const {return _end;}
private:
    int ValidateAndConvert(int hour, int min, int sec);
    
    bool equal(time_t) const;
private:
    int _start;
    int _end;
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
}

bool operator == (const time_range&, const time_range&);

enum ExchangeName: char;
UnorderedSet<time_range> GetWorkingRange(ExchangeName);