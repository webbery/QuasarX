#pragma once
#include "StrategyNode.h"

class FunctionNode: public QNode {
public:
    ~FunctionNode();

    virtual bool Init(DataContext& context, const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);

    void SetFunctionName(const String& name) { _funcionName = name; }

    template<typename T>
    void AddArgument(const String& name, T val) {
        _args[name] = val;
    }
    
private:

    
private:
    String _funcionName;
    Map<String, std::variant<int>> _args;
    ICallable* _callable = nullptr;
};
