#pragma once
#include "StrategyNode.h"

class FunctionNode: public QNode {
public:
    static const nlohmann::json getParams();
    RegistClassName(FunctionNode);
    
public:
    FunctionNode(Server* server);
    ~FunctionNode();

    virtual bool Init(const nlohmann::json& config);

    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;

    virtual Map<String, ArgType> out_elements();
    
    virtual void UpdateLabel(const String& label);

    template<typename T>
    void AddArgument(const String& name, T val) {
        _args[name] = val;
    }
    
    Server* GetServer() const { return _server; }
private:

    
private:
    Server* _server;
    Map<String, std::variant<int>> _args;
    ICallable* _callable = nullptr;
    Map<String, ArgType> _params;
    // 输出的数据名
    Map<String, ArgType> _outputs;
    // 输入 key 到输出 key 的映射（保证多标的顺序对应）
    Map<String, String> _param_to_output_map;

    String _label;
};

List<String> GetAllFunctionNames();