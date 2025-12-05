#pragma once
#include "std_header.h"

struct symbol_t;
struct TradeReport;
class Server;
using crash_flow_t = List<Pair<symbol_t, TradeReport>>;
// 数据上下文，用于管理节点间传输的数据
class DataContext {
public:
    DataContext(const String& strategy, Server* server);

    feature_t& get(const String& name);
    const feature_t& get(const String& name) const;

    void set(const String& name, const feature_t& f) {
        _outputs[name] = f;
    }

    void add(const String& name, feature_t value);

    bool exist(const String& name);

    void erase(const String& name) {
        _outputs.erase(name);
    }
    
    void SetEpoch(uint64_t epoch) {
        _epoch = epoch;
    }

    uint64_t GetEpoch() {
        return _epoch;
    }

    void SetTime(time_t t);
    const List<time_t>& GetTime() const;
    time_t Current();
private:
    uint64_t _epoch = 0;
    List<time_t> _times;

    // TODO: 节点的输出数据，待优化
    Map<String, feature_t> _outputs;
};
