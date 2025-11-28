#pragma once
#include "std_header.h"
#include "json.hpp"
#include <functional>

class ICallable;
// 数据上下文，用于管理节点间传输的数据
class DataContext {
public:
    feature_t& get(const String& name);

    void set(const String& name, const feature_t& f) {
        _outputs[name] = f;
    }

    void add(const String& name, double value);

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

    void SetTime(time_t t) { _time = t; }
    time_t GetTime() { return _time; }
private:
    uint64_t _epoch = 0;
    time_t _time = 0;

    // TODO: 节点的输出数据，待优化
    Map<String, feature_t> _outputs;
};

enum ArgType {
    Integer,
    Double,
};

class QNode {
    using Edges = MultiMap<String, QNode*>;
public:
    virtual ~QNode(){}
    virtual bool Init(const nlohmann::json& config) = 0;
    /**
     * @brief 对输入数据做处理，并返回处理后的数据
     */
    virtual bool Process(const String& strategy, DataContext& context) = 0;
    virtual void Connect(QNode* next, const String& from, const String& to) {
        _outs.insert({from, next});
        next->_ins.insert({to, this});
    }
    /**
     * @brief 返回输出结果在context中的输出名
     */
    virtual Map<String, ArgType> out_elements();
    
    // const nlohmann::json& getParams() { return _params; }
    
    String name() const { return _name; }
    void setName(const String& name){ _name = name; }

    size_t in_degree() const { return _ins.size(); }
    size_t out_degree() const { return _outs.size(); }

    const Edges& outs() const { return _outs; }
    const Edges& ins() const { return _ins; }

protected:
    String _name;
    // nlohmann::json _params;
    // key是handle名
    Edges _outs;
    Edges _ins;
};

class OperationNode: public QNode {
public:
    virtual bool Init(const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);

    // 解析表达式，构建函数对象
    bool parseFomula(const String& formulas);

private:
    std::function<void ()> _callable;
};
