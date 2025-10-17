#pragma once
#include "Feature.h"
#include "server.h"
#include "std_header.h"
#include "json.hpp"
#include "Util/system.h"
#include <functional>
#include "BrokerSubSystem.h"

class ICallable {
public:
};

class MA: public ICallable {
public:
};

class QNode {
public:
    virtual ~QNode(){}
    /**
     * @brief 对输入数据做处理，并返回处理后的数据
     */
    virtual feature_t Process(const DataFeatures& org, const feature_t& input) = 0;
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

class InputNode : public QNode {
public:
    virtual feature_t Process(const DataFeatures& org, const feature_t& input);

    void AddSymbol(symbol_t symbol) { _symbols.insert(symbol); }

    void EraseSymbol(symbol_t symbol) { _symbols.erase(symbol); }

    void Connect(QNode* next, const String& from, const String& to);
private:
    Set<symbol_t> _symbols;
};

class OperationNode: public QNode {
public:
    virtual feature_t Process(const DataFeatures& org, const feature_t& input);

    // 解析表达式，构建函数对象
    bool parseFomula(const String& formulas);

private:
    std::function<void ()> _callable;
};

class FunctionNode: public QNode {
public:
    ~FunctionNode();

    virtual feature_t Process(const DataFeatures& org, const feature_t& input);

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
    virtual feature_t Process(const DataFeatures& org, const feature_t& input);
};

class StatisticNode: public QNode {
public:
    virtual feature_t Process(const DataFeatures& org, const feature_t& input);

    void AddIndicator(StatisticIndicator ind) {
        _indicators.insert(ind);
    }
private:
    Set<StatisticIndicator> _indicators;
};

// 构建买入/卖出信号
class SignalNode: public QNode {
public:
    SignalNode(Server* server);

    virtual feature_t Process(const DataFeatures& org, const feature_t& input);
private:
    // 解析表达式，构建函数对象
    bool parseFomula(const String& formulas);

private:
    Server* _server;
};