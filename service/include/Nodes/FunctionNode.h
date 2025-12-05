#pragma once
#include "StrategyNode.h"

class FunctionNode: public QNode {
public:
    static List<String> GetNames();
    static List<String> GetParams(const String& name);
    
public:
    FunctionNode(Server* server);
    ~FunctionNode();

    virtual bool Init(const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);

    virtual Map<String, ArgType> out_elements();
    
    virtual void UpdateLabel(const String& label);

    template<typename T>
    void AddArgument(const String& name, T val) {
        _args[name] = val;
    }
    
private:

    
private:
    Server* _server;
    Map<String, std::variant<int>> _args;
    ICallable* _callable = nullptr;
    Map<String, ArgType> _params;
    // 输出的数据名
    Map<String, ArgType> _outputs;

    String _label;
};
