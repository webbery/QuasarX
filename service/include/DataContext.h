#pragma once
#include "std_header.h"
#include "Util/system.h"

struct TradeReport;
class Server;
using crash_flow_t = List<Pair<symbol_t, TradeReport>>;

using context_t = std::variant<bool, String, uint64_t, Vector<float>, List<symbol_t>, double, Vector<double>, Vector<uint64_t>, Eigen::MatrixXd>;
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

    symbol_t GetSymbol() const { return _symbol; }

    void Consume() { _executed = true; }
    bool IsConsume() { return _executed; }

    const TradeAction& Action() const { return _action; }
private:
    symbol_t _symbol;
    TradeAction _action;
    int _quantity;           // 数量
    double _price;           // 建议价格
    bool _executed: 1 = false;   // 是否已执行
    std::chrono::system_clock::time_point _create_time;
};

enum class SignalSource: char {
    STRATEGY_DAILY,
    STRATEGY_HOURLY,
    STRATEGY_MINUTE,
    MANUAL
};

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

    // feature_t& get(const String& name); 
    // const feature_t& get(const String& name) const;

    template<typename T>
    void set(const String& name, const T& f) {
        _outputs[name] = f;
    }

    void add(const String& name, feature_t value);
    template<typename T>
    void add(const String& name, const T& value) {
        set(name, value);
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

    std::unordered_map<symbol_t, TradeSignal*> _signals;
    List<ISignalObserver*> _signalObservers;

    // TODO: 节点的输出数据，待优化
    Map<String, context_t> _outputs;
};
