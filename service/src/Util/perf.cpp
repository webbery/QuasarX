//
// 通用性能计时原语的实现部分
//
// 目前只有 perf-cpp 硬件计数器（PerfCppCounters）需要 out-of-line 代码：
// 它的实现要在 include perf-cpp 头文件的前提下保持 DataContext.h 等
// 包含方不必拖进 perf-cpp 的重量级头文件。
//
#include "Util/perf.h"

#ifdef HAVE_PERF_CPP
#include "Util/log.h"

#include <perfcpp/exception.hpp>

#include <algorithm>
#include <string>
#include <tuple>

namespace {

/// 在计数器组里注册一个事件，返回其成员下标；本机不支持该事件时返回 -1
///
/// perf-cpp 的 counter() 返回所有 PMU 上同名事件的列表，优先取 "cpu" PMU。
/// 事件名在不同内核版本上不完全一致（cycles 叫 "cpu-cycles"，不叫 "cycles"），
/// 所以每个逻辑指标给一组候选名，逐个试。
int addEvent(perf::Group& group, const std::vector<const char*>& names) {
    const auto& def = perf::CounterDefinition::global();
    for (const char* name : names) {
        auto matches = def.counter(std::string(name));
        if (matches.empty()) continue;

        auto it = std::find_if(matches.begin(), matches.end(),
            [](const auto& t) { return std::get<0>(t) == "cpu"; });
        if (it == matches.end()) it = matches.begin();

        group.add(std::get<2>(*it));
        return static_cast<int>(group.size()) - 1;
    }
    return -1;
}

/// 取计数器组中第 index 个成员在本次 read() 里的计数值
double memberValue(const perf::CounterValues<perf::Group::MAX_MEMBERS>& vals,
                   const perf::Group& group, int index) {
    if (index < 0 || static_cast<size_t>(index) >= group.size()) return 0.0;
    auto v = vals.value(group.member(static_cast<size_t>(index)).id());
    return v ? static_cast<double>(*v) : 0.0;
}

/// 消耗一点 CPU 时间，让自探测时计数器确实有非零读数
void spinBriefly() {
    volatile double x = 0;
    for (long i = 0; i < 2000000; ++i) x += i * 1.000001;
}

/// 从一次 read() 的结果里抽出各逻辑指标的读数
PerfHwSnapshot snapshotOf(const perf::CounterValues<perf::Group::MAX_MEMBERS>& vals,
                          const perf::Group& group,
                          int idxCycles, int idxInstructions, int idxCacheMisses, int idxBranchMisses) {
    PerfHwSnapshot s;
    s.cycles = memberValue(vals, group, idxCycles);
    s.instructions = memberValue(vals, group, idxInstructions);
    s.cacheMisses = memberValue(vals, group, idxCacheMisses);
    s.branchMisses = memberValue(vals, group, idxBranchMisses);
    return s;
}

}   // namespace
#endif  // HAVE_PERF_CPP

PerfCppCounters::~PerfCppCounters() {
#ifdef HAVE_PERF_CPP
    if (_started) {
        try {
            _group.stop();
        } catch (...) {
            // 析构里不做任何事：计数器关不掉不影响进程退出
        }
    }
#endif
}

bool PerfCppCounters::open() {
#ifdef HAVE_PERF_CPP
    if (_available) return true;

    // 非特权进程只能测用户态：paranoid >= 2 时 kernel 态会被内核拒绝
    perf::Config cfg;
    cfg.include_user(true);
    cfg.include_kernel(false);
    cfg.include_hypervisor(false);
    cfg.include_guest(false);
    cfg.include_idle(false);
    // ⚠ 不要设 include_host(false)（即 exclude_host）。
    // 实测 kernel 5.15 + 原生 x86 PMU：带上这一位后 open() 不报错，
    // 但每次 read() 都返回 0 —— 计数器静默失效，比打不开还难查。
    // 这是逐指标实测出来的（guest/idle 单独关都正常，只有 host 会归零）。
    cfg.include_host(true);
    // worker 线程里可能派生过子线程（OpenMP / xgboost），一并计入
    cfg.include_child_threads(true);

    try {
        _idxCycles = addEvent(_group, {"cpu-cycles", "cycles"});
        _idxInstructions = addEvent(_group, {"instructions"});
        _idxCacheMisses = addEvent(_group, {"cache-misses"});
        _idxBranchMisses = addEvent(_group, {"branch-misses"});

        if (_idxCycles < 0 && _idxInstructions < 0) {
            _error = "no supported hardware events on this CPU";
            return false;
        }

        _group.open(cfg);
        _group.start();
        _started = true;

        // 自探测 read() 的语义：连续读两次，第二次若显著小于第一次，
        // 说明 read() 会复位（增量语义）；否则是累计语义，调用方自己求差。
        // 实测 perf-cpp v1.1.1 的 Group::read() 是累计语义（group.get() 恒为 0，
        // 只有传出去的 CounterValues 有效），这里不假设，靠探测决定。
        spinBriefly();
        perf::CounterValues<perf::Group::MAX_MEMBERS> vals;
        _group.read(vals);
        const PerfHwSnapshot probe1 = snapshotOf(vals, _group,
            _idxCycles, _idxInstructions, _idxCacheMisses, _idxBranchMisses);
        _group.read(vals);
        const PerfHwSnapshot probe2 = snapshotOf(vals, _group,
            _idxCycles, _idxInstructions, _idxCacheMisses, _idxBranchMisses);
        _readResets = (probe2.cycles < probe1.cycles);

        // 读数恒为 0 说明计数器没真正工作（例如带了内核不支持的 exclude 标志：
        // open() 成功但一直读 0）。这种情况必须显式降级，否则报告里会是一堆
        // 看起来"很健康"的 0，比缺数据更误导。
        if (probe2.cycles <= 0 && probe2.instructions <= 0) {
            _error = "counters opened but always read zero (unsupported perf attribute bits)";
            _available = false;
            return false;
        }

        // 以自探测读数为基线：第一次 read() 只应归因 open() 之后的那一段，
        // 不该把初始化成本算进第一个 epoch 的数据推进桶。
        _prev = probe2;

        _available = true;
        return true;
    }
    catch (const perf::CannotOpenCounterError& e) {
        // 最常见：kernel.perf_event_paranoid 过高，非特权进程打不开硬件计数器
        _error = e.what();
        _available = false;
        return false;
    }
    catch (const std::exception& e) {
        _error = e.what();
        _available = false;
        return false;
    }
#else
    _error = "perf-cpp not compiled in (HAVE_PERF_CPP undefined)";
    _available = false;
    return false;
#endif
}

void PerfCppCounters::start() {
#ifdef HAVE_PERF_CPP
    if (!_available || _started) return;
    try {
        _group.start();
        _started = true;
    }
    catch (const std::exception&) {
        _available = false;
        _started = false;
    }
#endif
}

void PerfCppCounters::stop() {
#ifdef HAVE_PERF_CPP
    if (!_available || !_started) return;
    try {
        _group.stop();
    }
    catch (const std::exception&) {
        // 停止失败不影响回测结果
    }
    _started = false;
#endif
}

void PerfCppCounters::read(PerfHwSnapshot& out) {
#ifdef HAVE_PERF_CPP
    if (!_available) return;
    try {
        perf::CounterValues<perf::Group::MAX_MEMBERS> vals;
        _group.read(vals);

        const PerfHwSnapshot cur = snapshotOf(vals, _group,
            _idxCycles, _idxInstructions, _idxCacheMisses, _idxBranchMisses);

        if (_readResets) {
            // 增量语义：本次读到的就是自上次读以来的量
            out = cur;
        }
        else {
            // 累计语义：自己求差
            out.cycles = cur.cycles - _prev.cycles;
            out.instructions = cur.instructions - _prev.instructions;
            out.cacheMisses = cur.cacheMisses - _prev.cacheMisses;
            out.branchMisses = cur.branchMisses - _prev.branchMisses;
        }
        _prev = cur;
    }
    catch (const std::exception&) {
        // 读失败（fd 失效等）→ 降级为不可用，后续 read() 变成空操作
        _available = false;
    }
#endif
}
