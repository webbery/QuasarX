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
    
private:
    // TODO: 节点的输出数据，待优化
    Map<String, feature_t> _outputs;
};

class QNode {
public:
    virtual ~QNode(){}
    /**
     * @brief 对输入数据做处理，并返回处理后的数据
     */
    virtual bool Process(const String& strategy, DataContext& context, const DataFeatures& org) = 0;
    virtual void Connect(QNode* next, const String& from, const String& to) {
        _outs[from] = next;
        next->_ins[to] = this;
    }
    

    void Update(const nlohmann::json& args) {
        _params = args;
    }

    const nlohmann::json& getParams() { return _params; }
    
    String name() const { return _name; }
    void setName(const String& name){ _name = name; }

    size_t in_degree() const { return _ins.size(); }
    size_t out_degree() const { return _outs.size(); }

    const Map<String, QNode*>& outs() const { return _outs; }
    const Map<String, QNode*>& ins() const { return _ins; }

protected:
    String _name;
    nlohmann::json _params;
    Map<String, QNode*> _outs;
    Map<String, QNode*> _ins;
};

class OperationNode: public QNode {
public:
    virtual bool Process(const String& strategy, DataContext& context, const DataFeatures& org);

    // 解析表达式，构建函数对象
    bool parseFomula(const String& formulas);

private:
    std::function<void ()> _callable;
};

class FunctionNode: public QNode {
public:
    ~FunctionNode();

    virtual bool Process(const String& strategy, DataContext& context, const DataFeatures& org);

    void SetFunctionName(const String& name) { _funcionName = name; }

    template<typename T>
    void AddArgument(const String& name, T val) {
        _args[name] = val;
    }
    
private:
    bool Init();
    
private:
    String _funcionName;
    Map<String, std::variant<int>> _args;
    ICallable* _callable = nullptr;
};

class FeatureNode: public QNode {
public:
    virtual bool Process(const String& strategy, DataContext& context, const DataFeatures& org);
};

class StatisticNode: public QNode {
public:
    virtual bool Process(const String& strategy, DataContext& context, const DataFeatures& org);

    void AddIndicator(StatisticIndicator ind) {
        _indicators.insert(ind);
    }

private:
    double Sharp(const Vector<double>&, double freerate);
private:
    Set<StatisticIndicator> _indicators;
};

class FormulaParser;
// 构建买入/卖出信号
class SignalNode: public QNode {
public:
    SignalNode(Server* server);
    ~SignalNode();

    virtual bool Process(const String& strategy, DataContext& context, const DataFeatures& org);
private:
    // 解析表达式，构建函数对象
    bool parseFomula(const String& formulas);


private:
    Server* _server;
    FormulaParser* _buyParser;
    FormulaParser* _sellParser;
};