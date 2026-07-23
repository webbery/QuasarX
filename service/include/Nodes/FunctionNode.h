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
    // 从 _ins/上游 _outs 连接信息解析实际输入映射
    Map<String, String> resolveInputConnections();

private:
    Server* _server;
    Map<String, std::variant<int>> _args;
    Map<String, ArgType> _params;
    // 输出的数据名
    Map<String, ArgType> _outputs;

    // ── 新架构：按 symbol 分组处理 ──
    // 从连接信息解析的实际输入映射: slot → context key
    Map<String, String> _resolvedInputs;
    // 每个 symbol 独立的 callable 实例（状态隔离）
    Map<String, ICallable*> _callables;

    String _label;
};

List<String> GetAllFunctionNames();