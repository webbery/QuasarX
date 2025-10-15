#pragma once
#include "std_header.h"
#include "json.hpp"
#include "Util/system.h"

class QNode {
public:
    virtual ~QNode(){}
    /**
     * @brief 对输入数据做处理，并返回处理后的数据
     */
    virtual List<QNode*> Process(const List<QNode*>& input) = 0;
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
protected:
    String _name;
    nlohmann::json _params;
    Map<String, QNode*> _outs;
    Map<String, QNode*> _ins;
};

class InputNode : public QNode {
public:
    virtual List<QNode*> Process(const List<QNode*>& input);

    bool parseFormula(const String& formulas);

    void AddSymbol(symbol_t symbol) { _symbols.insert(symbol); }

    void EraseSymbol(symbol_t symbol) { _symbols.erase(symbol); }

    void Connect(QNode* next, const String& from, const String& to);
private:
    Set<symbol_t> _symbols;
};

class OperationNode: public QNode {
public:
    virtual List<QNode*> Process(const List<QNode*>& input);
};

class FunctionNode: public QNode {
public:
    virtual List<QNode*> Process(const List<QNode*>& input);

    void SetFunctionName(const String& name) { _funcionName = name; }

    template<typename T>
    void AddArgument(const String& name, T val) {
        _args[name] = val;
    }
    
private:
    String _funcionName;
    Map<String, std::variant<int>> _args;
};

class FeatureNode: public QNode {
public:
    virtual List<QNode*> Process(const List<QNode*>& input);
};

class OutputNode: public QNode {
public:
    virtual List<QNode*> Process(const List<QNode*>& input);
};