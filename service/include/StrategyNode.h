#pragma once
#include "std_header.h"
#include "Feature.h"
#include "server.h"
#include "json.hpp"
#include "Util/system.h"
#include <functional>
#include "BrokerSubSystem.h"

class ICallable;
// 数据上下文，用于管理节点间传输的数据
class DataContext {
public:
    feature_t& get(const String& name) {
        return _outputs[name];
    }

    void add(const String& name, const feature_t& f) {
        _outputs[name] = f;
    }

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

class QNode {
public:
    virtual ~QNode(){}
    virtual bool Init(DataContext& context, const nlohmann::json& config) = 0;
    /**
     * @brief 对输入数据做处理，并返回处理后的数据
     */
    virtual bool Process(const String& strategy, DataContext& context) = 0;
    virtual void Connect(QNode* next, const String& from, const String& to) {
        _outs[from] = next;
        next->_ins[to] = this;
    }
    
    // const nlohmann::json& getParams() { return _params; }
    
    String name() const { return _name; }
    void setName(const String& name){ _name = name; }

    size_t in_degree() const { return _ins.size(); }
    size_t out_degree() const { return _outs.size(); }

    const Map<String, QNode*>& outs() const { return _outs; }
    const Map<String, QNode*>& ins() const { return _ins; }

protected:
    String _name;
    // nlohmann::json _params;
    Map<String, QNode*> _outs;
    Map<String, QNode*> _ins;
};

class OperationNode: public QNode {
public:
    virtual bool Init(DataContext& context, const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);

    // 解析表达式，构建函数对象
    bool parseFomula(const String& formulas);

private:
    std::function<void ()> _callable;
};

class FeatureNode: public QNode {
public:
    virtual bool Init(DataContext& context, const nlohmann::json& config);
    virtual bool Process(const String& strategy, DataContext& context);
};

class FormulaParser;
// 构建买入/卖出信号
class SignalNode: public QNode {
public:
    SignalNode(Server* server);
    ~SignalNode();

    virtual bool Init(DataContext& context, const nlohmann::json& config);
    virtual bool Process(const String& strategy, DataContext& context);

    bool ParseBuyExpression(const String& expression);
    bool ParseSellExpression(const String& expression);

private:


private:
    Server* _server;
    FormulaParser* _buyParser;
    FormulaParser* _sellParser;
};