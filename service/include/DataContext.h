#pragma once
#include "std_header.h"
#include "Util/system.h"
#include "Nodes/ExecutionPlan.h"
#include "Bridge/exchange.h"
#include "Bridge/SIM/BacktestContext.h"
#include "RiskContext.h"

#include <chrono>

struct TradeReport;
class Server;
using crash_flow_t = List<Pair<symbol_t, TradeReport>>;

// 数据warmup期间的填充方式
enum class WarmupFillType: char {
    Skip,
    FillNan,    // 默认填充NAN
};

// 交易操作类型
enum class TradeAction: char {
    HOLD,
    BUY,
    SELL,
    EXEC,
};
// 交易决策信号
class TradeSignal {
public:
    TradeSignal(symbol_t symbol, TradeAction act): _symbol(symbol), _action(act) {}

    virtual ~TradeSignal(){}

    // Symbol
    symbol_t GetSymbol() const { return _symbol; }

    // Action
    const TradeAction& GetAction() const { return _action; }
    void SetAction(TradeAction act) { _action = act; }

    unsigned char _flag = 0;  // 0=开仓, 1=平仓
    void SetFlag(unsigned char f) { _flag = f; }
    unsigned char GetFlag() const { return _flag; }

    // Quantity
    int GetQuantity() const { return _quantity; }
    void SetQuantity(int qty) { _quantity = qty; }

    // Price
    double GetPrice() const { return _price; }
    void SetPrice(double price) { _price = price; }

    // Executed
    void Consume() { _executed = true; }
    bool IsConsume() const { return _executed; }

    // Create time (system clock, only for realtime mode)
    std::chrono::system_clock::time_point GetCreateTime() const { return _create_time; }
    void SetCreateTime(std::chrono::system_clock::time_point t) { _create_time = t; }

    // Backtest time (bar time, only valid in backtest mode)
    time_t GetBacktestTime() const { return _backtestTime; }
    void SetBacktestTime(time_t t) { _backtestTime = t; }

    // 来源标识：区分 SignalNode 评估产生的信号 vs ExecuteNode 默认补的 HOLD
    void SetDefaultHold(bool v = true) { _isDefaultHold = v; }
    bool IsDefaultHold() const { return _isDefaultHold; }

private:
    symbol_t _symbol;
    TradeAction _action;
    int _quantity = 0;           // 数量
    double _price = 0.0;         // 建议价格
    bool _executed: 1 = false;   // 是否已执行
    bool _isDefaultHold: 1 = false; // 是否 ExecuteNode 默认补的 HOLD（非 SignalNode 评估产生）
    std::chrono::system_clock::time_point _create_time;
    time_t _backtestTime = 0;    // 回测时间 (信号触发时的 Bar 时间)
};

// enum class SignalSource: char {
//     STRATEGY_DAILY,
//     STRATEGY_HOURLY,
//     STRATEGY_MINUTE,
//     MANUAL
// };

/**
    * @brief 获取 context_t 的实际类型名称
    * @param ctx 上下文数据
    * @return 类型名称字符串（如 "double", "vector<double>", "string" 等）
    */
String get_context_type_name(const context_t& ctx);

/**
    * @brief 格式化 context_t 的调试信息（类型 + 大小/值）
    * @param ctx 上下文数据
    * @return 格式化的调试信息字符串
    */
String format_context_info(const context_t& ctx);

class ITimingStrategy;
class DataContext;
class ISignalObserver {
public:
    virtual ~ISignalObserver(){}
    virtual void OnSignalConsume(const String& strategy, TradeSignal* , const DataContext &context) = 0;
    virtual void OnSignalAdded(TradeSignal* ) {};
    virtual void OnSignalExpired(TradeSignal*) {};
    virtual void RegistTimingStrategy(ITimingStrategy*) {};
    virtual void UnregistTimingStrategy(ITimingStrategy*) {};
};

// 数据上下文，用于管理节点间传输的数据
class DataContext {
public:
    DataContext(const String& strategy, Server* server);
    // 禁止复制和移动
    DataContext(const DataContext&) = delete;
    DataContext& operator=(const DataContext&) = delete;

    ~DataContext();

    // ── 性能剖析（回测排查用，默认关闭）──
    // DataContext 由策略 worker 线程独占访问（栈局部对象），计数无需原子操作。
    // 关闭时每个原语只多一次 bool 判断；开启时每次调用读两次 steady_clock（~25ns/次）。
    struct PerfCounter {
        uint64_t calls = 0;    // 调用次数
        double totalMs = 0;    // 累计耗时
        double maxMs = 0;      // 单次最大耗时（识别深拷贝大向量这类尖峰）
    };
    struct PerfStat {
        bool enabled = false;
        PerfCounter get, set, add, exist;
        uint64_t keys = 0;     // _outputs 的 key 数量（GetPerfStat 时刷新）
        uint64_t bytes = 0;    // _outputs 估算占用字节（key + 数值负载）
        uint64_t ops() const { return get.calls + set.calls + add.calls + exist.calls; }
        double totalMs() const { return get.totalMs + set.totalMs + add.totalMs + exist.totalMs; }
        void reset() { get = {}; set = {}; add = {}; exist = {}; }
    };

    // RAII 计时：未开启时只是一次分支判断，不读时钟
    class PerfScope {
    public:
        PerfScope(bool enabled, PerfCounter& counter)
            : _counter(enabled ? &counter : nullptr) {
            if (_counter) _start = std::chrono::steady_clock::now();
        }
        ~PerfScope() {
            if (_counter) {
                double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - _start).count();
                _counter->calls++;
                _counter->totalMs += ms;
                if (ms > _counter->maxMs) _counter->maxMs = ms;
            }
        }
        PerfScope(const PerfScope&) = delete;
        PerfScope& operator=(const PerfScope&) = delete;
    private:
        PerfCounter* _counter = nullptr;
        std::chrono::steady_clock::time_point _start{};
    };

    void EnablePerfProfile(bool on) { _perf.enabled = on; }
    bool IsPerfProfileEnabled() const { return _perf.enabled; }
    void ResetPerfStat() { _perf.reset(); }
    /**
     * @brief 读取剖析结果；顺带刷新 _outputs 的 keys/bytes 规模快照
     * @note 只在快照输出时调用，遍历 _outputs 有一定成本，不要放在热路径
     */
    const PerfStat& GetPerfStat() {
        uint64_t bytes = 0;
        for (const auto& [key, value] : _outputs) {
            bytes += key.size() + sizeof(void*) * 2;  // key 字符串 + 容器节点开销
            bytes += ValueBytes(value);
        }
        _perf.keys = _outputs.size();
        _perf.bytes = bytes;
        return _perf;
    }

    template<typename T>
    T& get(const String& name) {
        PerfScope scope(_perf.enabled, _perf.get);
        return std::get<T>(_outputs.at(name));
    }
    template<typename T>
    const T& get(const String& name) const {
        PerfScope scope(_perf.enabled, _perf.get);
        return std::get<T>(_outputs.at(name));
    }

    context_t& get(const String& name) {
        PerfScope scope(_perf.enabled, _perf.get);
        return _outputs.at(name);
    }

    // context_t& get(const String& name); 
    // const context_t& get(const String& name) const;

    template<typename T>
    void set(const String& name, const T& f) {
        PerfScope scope(_perf.enabled, _perf.set);
        _outputs[name] = f;
    }

    void add(const String& name, context_t value);
    template<typename T>
    void add(const String& name, const T& value) {
        PerfScope scope(_perf.enabled, _perf.add);
        auto& item = _outputs[name];
        std::visit([&name, &value, this](auto&& v) {
            using CTX_T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<CTX_T, Vector<double>> && std::is_same_v<T, double>) {
                v.emplace_back(std::move(value));
            }
            else {
                // 直接赋值而非调用 set()：避免同一操作被计入 add 与 set 两个桶
                _outputs[name] = value;
            }
            }, item);
    }

    bool exist(const String& name);

    void erase(const String& name);
    
    void SetEpoch(uint64_t epoch) {
        _epoch = epoch;
    }

    uint64_t GetEpoch() const {
        return _epoch;
    }

    // 日终决策锁定：仅最后一根 bar 产出 BUY/SELL；前置 epoch 用于节点 warmup，
    // 评估但不累积决策。回测路径每根都是决策（默认 true）。
    void SetDecisionBar(bool is) {
        _isDecisionBar = is;
    }
    bool IsDecisionBar() const {
        return _isDecisionBar;
    }

    void SetTime(time_t t);
    const List<time_t>& GetTime() const;
    time_t Current();

    void EnableShareMemory(const String& name) {}

    const String& CurrentStrategy() const { return _strategy; }

    void AddSignal(TradeSignal* signal);

    void RegistSignalObserver(ISignalObserver*);

    void UnregisterObserver(ISignalObserver* observer);

    void ConsumeSignals();

    ExecutionPlan& GetExecutionPlan() {
        return _executionPlan;
    }

    RiskContext* GetRiskContext() {
        return &_risk_context;
    }

    double getAvailableCapital() const;

    void setInitialCapital(double capital);
    double getInitialCapital() const;
    // 策略资金管理（从资金池分配，同时用作收益率计算基准）
    void setCapital(double capital);
    double getCapital() const;
    void updateCapital(double delta);  // 成交时更新

    // QuoteInfo 存储和获取（用于影子模式）
    void SetQuote(symbol_t symbol, const QuoteInfo& quote);
    const QuoteInfo* GetQuote(symbol_t symbol) const;

    // ============ 多线程回测支持 ============

    /**
     * @brief 设置关联的回测运行 ID
     */
    void setBacktestRunId(uint16_t runId) { _backtestRunId = runId; }

    /**
     * @brief 获取关联的回测运行 ID
     */
    uint16_t getBacktestRunId() const { return _backtestRunId; }

    /**
     * @brief 获取回测上下文（通过 server 查找）
     */
    BacktestContext* getBacktestContext();

    TradeSignal* getSignalBySymbol(symbol_t);

    /**
     * @brief 获取所有 signals（只读，用于 ExecuteNode 遍历检查）
     */
    const std::unordered_map<symbol_t, TradeSignal*>& getAllSignals() const { return _signals; }

    // ============ Warmup 管理 ============

    /**
     * @brief 设置预热期数（回测模式下 FunctionNode 需要预热的 epoch 数）
     */
    void SetWarmupEpochs(int epochs) { _warmupEpochs = epochs; }

    /**
     * @brief 获取预热期数
     */
    int GetWarmupEpochs() const { return _warmupEpochs; }

    /**
     * @brief 检查当前是否处于预热期
     */
    bool IsInWarmup() const { return _epoch <= (uint64_t)_warmupEpochs; }

    /**
     * @brief 获取排除 warmup 后的有效 epoch 计数
     *         warmup 期间返回 0
     */
    int GetEffectiveEpoch() const {
        if (_epoch <= (uint64_t)_warmupEpochs) return 0;
        return (int)(_epoch - _warmupEpochs);
    }

    // ============ 回测 Exchange 类型（策略初始化时设置，支持多个） ============

    void addExchangeType(ExchangeType type);
    const Set<ExchangeType>& getExchangeTypes() const { return _exchangeTypes; }

    // ============ 节点输出收集（XGBoost 训练用） ============

    /**
     * @brief 遍历 _outputs，将所有 Vector<double> 类型的值拷贝到 target
     */
    void CollectNumericOutputs(Map<String, Vector<double>>& target);

private:
    // 移除过期信号
    void cleanupExpiredSignals();


     // 标记信号为已执行
    bool markSignalExecuted(const std::string& signal_id);

    // context_t 数值负载的估算字节数（性能剖析用）
    static uint64_t ValueBytes(const context_t& value);
private:
    uint64_t _epoch = 0;
    const String _strategy;
    List<time_t> _times;
    Server* _server;

    std::unordered_map<symbol_t, TradeSignal*> _signals;
    List<ISignalObserver*> _signalObservers;
    ExecutionPlan _executionPlan;

    // TODO: 节点的输出数据，待优化
    std::unordered_map<String, context_t> _outputs;

    // 当前 Bar 的 QuoteInfo（用于影子模式）
    Map<symbol_t, QuoteInfo> _quotes;

    // 风控上下文（独立于节点数据通道）
    RiskContext _risk_context;

    double _initialCapital = 0.0;
    // 策略级资金
    double _capital = 0.0;   // 从资金池分配的资金（同时用作收益率计算基准）

    // 关联的回测运行 ID（0 表示未关联）
    uint16_t _backtestRunId{0};

    // 预热期数（回测模式下 FunctionNode 需要预热的 epoch 数）
    int _warmupEpochs = 0;

    // 决策锁定标志（默认 true 兼容回测路径）
    bool _isDecisionBar = true;

    // 策略初始化时设置的 Exchange 类型（支持股票+ETF 混合）
    Set<ExchangeType> _exchangeTypes;

    // 性能剖析统计（mutable：const get() 也要计时）
    mutable PerfStat _perf;
};
