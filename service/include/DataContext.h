#pragma once
#include "std_header.h"
#include "Util/system.h"
#include "Nodes/ExecutionPlan.h"
#include "Bridge/exchange.h"
#include "Bridge/SIM/BacktestContext.h"

struct TradeReport;
class Server;
using crash_flow_t = List<Pair<symbol_t, TradeReport>>;

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

    // Quantity
    int GetQuantity() const { return _quantity; }
    void SetQuantity(int qty) { _quantity = qty; }

    // Price
    double GetPrice() const { return _price; }
    void SetPrice(double price) { _price = price; }

    // Executed
    void Consume() { _executed = true; }
    bool IsConsume() const { return _executed; }

    // Create time
    std::chrono::system_clock::time_point GetCreateTime() const { return _create_time; }
    void SetCreateTime(std::chrono::system_clock::time_point t) { _create_time = t; }

private:
    symbol_t _symbol;
    TradeAction _action;
    int _quantity = 0;           // 数量
    double _price = 0.0;         // 建议价格
    bool _executed: 1 = false;   // 是否已执行
    std::chrono::system_clock::time_point _create_time;
};

enum class SignalSource: char {
    STRATEGY_DAILY,
    STRATEGY_HOURLY,
    STRATEGY_MINUTE,
    MANUAL
};

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

    template<typename T>
    T& get(const String& name) {
        return std::get<T>(_outputs.at(name));
    }
    template<typename T>
    const T& get(const String& name) const {
        return std::get<T>(_outputs.at(name));
    }

    context_t& get(const String& name) {
        return _outputs.at(name);
    }

    // context_t& get(const String& name); 
    // const context_t& get(const String& name) const;

    template<typename T>
    void set(const String& name, const T& f) {
        _outputs[name] = f;
    }

    void add(const String& name, context_t value);
    template<typename T>
    void add(const String& name, const T& value) {
        auto& item = _outputs[name];
        std::visit([&name, &value, this](auto&& v) {
            using CTX_T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<CTX_T, Vector<double>> && std::is_same_v<T, double>) {
                v.emplace_back(std::move(value));
            }
            else {
                set(name, value);
            }
            }, item);
    }

    bool exist(const String& name);

    void erase(const String& name);
    
    void SetEpoch(uint64_t epoch) {
        _epoch = epoch;
    }

    uint64_t GetEpoch() {
        return _epoch;
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

    double getAvailableCapital() const;

    // 初始本金管理（用于回测）
    void setInitialCapital(double capital);
    double getInitialCapital() const;

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

private:
    // 移除过期信号
    void cleanupExpiredSignals();

    TradeSignal* getSignalBySymbol(symbol_t);

     // 标记信号为已执行
    bool markSignalExecuted(const std::string& signal_id);
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

    // 初始本金（用于回测）
    double _initialCapital = 0.0;

    // 关联的回测运行 ID（0 表示未关联）
    uint16_t _backtestRunId{0};
};
