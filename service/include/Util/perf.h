#pragma once
//
// 通用性能计时原语
//
// PerfCounter / PerfScope 原本定义在 DataContext 里（策略图节点间数据传递的剖析），
// 它们本身与 DataContext 无关，这里抽出来作为通用件；DataContext 通过别名继续引用，
// 既有代码无需改动。
//
// PerfStages 用于多阶段计算链路（如波动率分析）按阶段名打点，
// 输出一行键序固定的汇总，便于优化前后 diff。
//
#include "std_header.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>

/// 计时时钟类型别名（单调时钟，不受系统时间调整影响）
using PerfClock = std::chrono::steady_clock;

/// 距 t0 过去了多少毫秒
inline double ms_since(PerfClock::time_point t0) {
    return std::chrono::duration<double, std::milli>(
        PerfClock::now() - t0).count();
}

/// 通用性能计数器：调用次数 / 累计耗时 / 单次最大耗时（用于发现尖峰）
struct PerfCounter {
    uint64_t calls = 0;    // 调用次数
    double totalMs = 0;    // 累计耗时
    double maxMs = 0;      // 单次最大耗时

    void add(double ms) {
        calls++;
        totalMs += ms;
        if (ms > maxMs) maxMs = ms;
    }
};

/// RAII 计时：未开启时只是一次分支判断，不读时钟
class PerfScope {
public:
    PerfScope(bool enabled, PerfCounter& counter)
        : _counter(enabled ? &counter : nullptr) {
        if (_counter) _start = std::chrono::steady_clock::now();
    }
    ~PerfScope() {
        if (_counter) _counter->add(ms_since(_start));
    }
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;
private:
    PerfCounter* _counter = nullptr;
    std::chrono::steady_clock::time_point _start{};
};

/// 空计时器占位：PerfScope 在 enabled=false 时不会写入它
inline PerfCounter& sinkCounter() {
    static PerfCounter sink;
    return sink;
}

/// 按阶段名组织的分段计时器
///
/// 用法:
///   PerfStages stages;
///   {
///       PERF_SCOPE(stages, "data_load");
///       ... 耗时代码 ...
///   }
///   stages.setValue("n", symbols.size());
///   INFO("[X][perf] {}", stages.summary());
class PerfStages {
public:
    explicit PerfStages(bool enabled = true) : _enabled(enabled) {}

    void setEnabled(bool on) { _enabled = on; }
    bool enabled() const { return _enabled; }

    /// 取（或创建）阶段计数器。std::map 扩容不失效引用，可安全持有。
    PerfCounter& stage(const String& name) {
        return _stages[name];
    }

    /// 记录标量（n / T / 对数等）；只保留 double 重载，避免 size_t/uint64_t 在 MSVC 上重定义
    void setValue(const String& key, double value) { _values[key] = value; }

    /// 记录一个命名项的耗时（用于定位慢标的）
    void addItem(const String& name, double ms) {
        _items.emplace_back(name, ms);
    }

    double stageMs(const String& name) const {
        auto it = _stages.find(name);
        return it == _stages.end() ? 0.0 : it->second.totalMs;
    }

    uint64_t stageCalls(const String& name) const {
        auto it = _stages.find(name);
        return it == _stages.end() ? 0 : it->second.calls;
    }

    /// 计时器自身存活时长
    double elapsedMs() const {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - _start).count();
    }

    /// 一行汇总：total + 标量 + 阶段（键序固定，便于 diff）
    String summary() const {
        std::ostringstream os;
        os << "total=" << fmtMs(elapsedMs()) << "ms";
        for (const auto& [key, value] : _values) {
            os << ' ' << key << '=' << fmtNum(value);
        }
        for (const auto& [name, counter] : _stages) {
            os << ' ' << name << "=(" << fmtMs(counter.totalMs) << "ms," << counter.calls
               << "x,max=" << fmtMs(counter.maxMs) << "ms)";
        }
        return os.str();
    }

    /// 最慢的若干命名项，如 "600519(12.3ms) 000001(9.1ms)"
    String itemsSummary(size_t n = 5) const {
        Vector<Pair<String, double>> sorted = _items;
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        std::ostringstream os;
        size_t count = 0;
        for (const auto& [name, ms] : sorted) {
            if (count++ >= n) break;
            if (count > 1) os << ' ';
            os << name << '(' << fmtMs(ms) << "ms)";
        }
        return os.str();
    }

private:
    static String fmtMs(double v) {
        std::ostringstream os;
        os << std::fixed << std::setprecision(1) << v;
        return os.str();
    }

    static String fmtNum(double v) {
        std::ostringstream os;
        os << std::fixed << std::setprecision(0) << v;
        return os.str();
    }

    bool _enabled = true;
    std::chrono::steady_clock::time_point _start = std::chrono::steady_clock::now();
    Map<String, PerfCounter> _stages;
    Map<String, double> _values;
    Vector<Pair<String, double>> _items;
};

#define PERF_CONCAT_(a, b) a##b
#define PERF_CONCAT(a, b) PERF_CONCAT_(a, b)

/// 作用域计时：PERF_SCOPE(stages, "stage_name")
#define PERF_SCOPE(stages, name) \
    ::PerfScope PERF_CONCAT(_perf_scope_, __COUNTER__){(stages).enabled(), (stages).stage(name)}
