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
// PerfCppCounters 是 perf-cpp 硬件计数器的封装（宏 HAVE_PERF_CPP 控制，
// 定义在 CMake 的 ENABLE_PERF_CPP 打开时）。它与 PerfScope 是互补关系：
//   PerfScope → 墙钟归因："慢在哪一段"（毫秒）
//   PerfCpp   → 微架构归因："这段慢是访存受限还是分支受限"（cycles / IPC / cache-miss）
//
#include "std_header.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>

#ifdef HAVE_PERF_CPP
#include <perfcpp/counter_definition.hpp>
#include <perfcpp/counter/group.hpp>
#endif

/// 计时时钟类型别名（单调时钟，不受系统时间调整影响）
using PerfClock = std::chrono::steady_clock;

/// 距 t0 过去了多少毫秒
inline double ms_since(PerfClock::time_point t0) {
    return std::chrono::duration<double, std::milli>(
        PerfClock::now() - t0).count();
}

/// 单次 steady_clock::now() 的成本（纳秒）
///
/// 进程内标定一次（函数局部 static，线程安全初始化）。
///
/// 为什么需要它：PerfScope 每次进出各读一次时钟，这两次读取本身要花时间，
/// 而它花的时间正好落在被测区间里 —— 插桩把被测代码拖慢了多少，不算出来
/// 就无法判断"测出来的数"到底可不可信。PerfCounter::clockReads 记录读了多少
/// 次，用本函数折算成 overheadMs()，报告里和净耗时一起给出。
inline double clock_read_cost_ns() {
    static const double ns = [] {
        constexpr int kSamples = 20000;
        // 取最小值而不是均值：均值会被中断/调度/频率切换拉高，实测同一进程
        // 不同轮次能在 15ns ~ 42ns 之间波动，导致 overhead 被高估 3 倍、
        // net 被过度扣减（甚至出现 "net 比一次哈希查找还快" 的荒谬值）。
        // 最小值收敛到 vDSO clock_gettime 的真实成本，稳定得多。
        double best = 1e9;
        for (int i = 0; i < kSamples; ++i) {
            auto a = PerfClock::now();
            auto b = PerfClock::now();
            const double d = std::chrono::duration<double, std::nano>(b - a).count();
            if (d < best) best = d;
        }
        return best;
    }();
    return ns;
}

/// 通用性能计数器：调用次数 / 累计耗时 / 单次最大耗时（用于发现尖峰）
struct PerfCounter {
    uint64_t calls = 0;    // 调用次数
    double totalMs = 0;    // 累计耗时
    double maxMs = 0;      // 单次最大耗时
    uint64_t clockReads = 0;  // 插桩读时钟次数（用于折算插桩开销）

    void add(double ms, uint64_t reads = 0) {
        calls++;
        totalMs += ms;
        clockReads += reads;
        if (ms > maxMs) maxMs = ms;
    }

    /// 插桩自身累计开销（毫秒）：每次 PerfScope 两次读时钟
    double overheadMs() const {
        return clockReads * clock_read_cost_ns() / 1e6;
    }

    /// 扣掉插桩开销后的净耗时（毫秒）
    double netMs() const {
        double net = totalMs - overheadMs();
        return net > 0 ? net : 0;
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
        // reads = 2：构造读一次、析构读一次，这两次的成本记进 counter，
        // 由 overheadMs() 折算，用于报告里区分"被测代码慢"和"插桩本身慢"
        if (_counter) _counter->add(ms_since(_start), 2);
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

// ── 逐 epoch 采样的分布分析 ──
//
// 回测耗时随 epoch 变化的形态是定位瓶颈的关键线索：
//   drift >> 1  → 成本随序列长度增长（O(n²) 症状，比如每 epoch append 的特征序列）
//   drift << 1  → 前置开销大（warmup / 懒加载 / 首次 JIT）
//   drift ≈ 1   → 稳定的常量开销

/// 逐 epoch 采样的分位数（拷贝一份后 nth_element，避免整表排序）
inline double epoch_percentile(Vector<float> samples, double q) {
    if (samples.empty()) return 0.0;
    size_t idx = std::min(samples.size() - 1, static_cast<size_t>(q * samples.size()));
    std::nth_element(samples.begin(), samples.begin() + idx, samples.end());
    return samples[idx];
}

/// 逐 epoch 采样的最大值
inline double epoch_max(const Vector<float>& samples) {
    return samples.empty() ? 0.0 : *std::max_element(samples.begin(), samples.end());
}

/// 首尾各 10% epoch 的均值比
///
/// >1 说明成本随数据增长（O(n²) 症状），<1 说明前置 warmup/懒加载开销大
inline double epoch_drift(const Vector<float>& samples) {
    size_t n = samples.size();
    if (n < 20) return 0.0;
    size_t w = std::max<size_t>(1, n / 10);
    double head = 0, tail = 0;
    for (size_t i = 0; i < w; ++i) head += samples[i];
    for (size_t i = n - w; i < n; ++i) tail += samples[i];
    if (head <= 0) return 0.0;
    return (tail / w) / (head / w);
}

// ── perf-cpp 硬件性能计数器 ──
//
// 三个必须记住的前提：
//
// 1) 计数器绑定「打开它的那个线程」。
//    perf_event_open 的 pid=0 表示 calling thread，所以必须在回测 worker 线程里
//    open()，不能作为 FlowSubsystem 的成员跨线程使用。
//
// 2) 打不开时必须优雅降级。
//    kernel.perf_event_paranoid >= 2 时非特权进程打不开硬件计数器
//    （>= 3 连软件事件都不行），perf-cpp 抛 perf::CannotOpenCounterError。
//    计数器缺失绝不能影响回测 —— open() 捕获所有异常并把 available() 置 false。
//
// 3) read() 的语义（增量 vs 累计）在不同 perf-cpp 版本 / 内核下不完全一致。
//    本封装在 open() 时做一次自探测（连续读两次，看第二次是不是回到 0），
//    然后统一对外提供「区间增量」语义，调用方不必关心底层是哪种。

/// 一段区间内的硬件计数器快照
struct PerfHwSnapshot {
    double cycles = 0;         // CPU 周期数
    double instructions = 0;   // 退休指令数
    double cacheMisses = 0;    // 缓存失效次数
    double branchMisses = 0;   // 分支预测失败次数

    /// 每周期指令数：<1 说明访存/分支受限，>1 说明计算受限
    double ipc() const {
        return cycles > 0 ? instructions / cycles : 0.0;
    }

    /// 每千条指令的缓存失效次数：越高说明越吃内存带宽
    double missPerKInstr() const {
        return instructions > 0 ? cacheMisses * 1000.0 / instructions : 0.0;
    }

    bool empty() const {
        return cycles == 0 && instructions == 0 && cacheMisses == 0 && branchMisses == 0;
    }
};

/// perf-cpp 硬件计数器组（线程内使用）
///
/// 用法：
///   PerfCppCounters hw;
///   if (hw.open()) hw.start();
///   ... 被测代码 ...
///   hw.read(snap);          // snap = 上次 read() 之后的增量
///   hw.stop();
///
/// open() 失败时 available() 为 false，read() 是空操作 —— 调用方不需要写
/// 任何 if 分支去区分"有计数器"和"没计数器"两种情况。
class PerfCppCounters {
public:
    PerfCppCounters() = default;
    ~PerfCppCounters();

    PerfCppCounters(const PerfCppCounters&) = delete;
    PerfCppCounters& operator=(const PerfCppCounters&) = delete;

    /// 打开计数器组；成功返回 true，失败返回 false，原因见 error()（不抛异常）
    bool open();

    /// 开始计数（幂等）
    void start();

    /// 停止计数（幂等）
    void stop();

    bool available() const { return _available; }

    /// 打开失败的原因（available() 为 false 时有意义）
    const String& error() const { return _error; }

    /// 读取自上次 read() 以来的增量；不可用时是空操作
    void read(PerfHwSnapshot& out);

private:
    bool _available = false;
    bool _started = false;
    String _error;

#ifdef HAVE_PERF_CPP
    perf::Group _group;
    // 逻辑指标 → 组成员下标；-1 表示该事件在本机不可用
    int _idxCycles = -1;
    int _idxInstructions = -1;
    int _idxCacheMisses = -1;
    int _idxBranchMisses = -1;

    // read() 是增量语义还是累计语义：open() 时自探测一次
    bool _readResets = true;
    PerfHwSnapshot _prev;   // 累计口径下的上一次读数，用于自己求差
#endif
};
