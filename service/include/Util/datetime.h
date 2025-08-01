#pragma once
#include "std_header.h"
#include <utility>

time_t FromStr(const String& str, const char* fmt = "%Y-%m-%d");
time_t FromTick(const String& str);

std::string ToString(time_t, const char* fmt = "%Y-%m-%d %H:%M:%S");

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
    String Date() const ;

    time_t Start() const { return _start; }
    time_t End() const { return _end; }

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
