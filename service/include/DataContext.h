#pragma once
#include "std_header.h"
#include "Util/system.h"

struct TradeReport;
class Server;
using crash_flow_t = List<Pair<symbol_t, TradeReport>>;

// 交易操作类型
enum class TradeAction {
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

    symbol_t GetSymbol() { return _symbol; }

    void Consume() { _executed = true; }
    bool IsConsume() { return _executed; }
private:
    symbol_t _symbol;
    TradeAction _action;
    int _quantity;           // 数量
    double _price;           // 建议价格
    bool _executed: 1 = false;   // 是否已执行
    std::chrono::system_clock::time_point _create_time;
};

class ISignalObserver {
public:
    virtual ~ISignalObserver(){}
    virtual void OnSignalConsume(TradeSignal* ) = 0;
    virtual void OnSignalAdded(TradeSignal* ) {};
    virtual void OnSignalExpired(TradeSignal* ) {};
};

// 数据上下文，用于管理节点间传输的数据
class DataContext {
public:
    DataContext(const String& strategy, Server* server);
    // 禁止复制和移动
    DataContext(const DataContext&) = delete;
    DataContext& operator=(const DataContext&) = delete;

    ~DataContext();

    feature_t& get(const String& name);
    const feature_t& get(const String& name) const;

    void set(const String& name, const feature_t& f);

    void add(const String& name, feature_t value);

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
    List<time_t> _times;

    std::unordered_map<symbol_t, TradeSignal*> _signals;
    List<ISignalObserver*> _observers;

    // TODO: 节点的输出数据，待优化
    Map<String, feature_t> _outputs;
};
