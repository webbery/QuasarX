#pragma once
#include "DataContext.h"
#include "json.hpp"
#include <functional>

class ICallable;

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
