#pragma once
#include "StrategyNode.h"
#include "server.h"

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

    // 上游视图频率：用于判断是否需要按视图频率调用 callable
    // 当视图频率低于加载频率时（如周频视图），callable 应只在有新数据时调用
    DataFrequencyType _viewFreq = DataFrequencyType::Day;
    // 每个 symbol 上一次处理的输入向量长度（用于检测新数据）
    Map<String, size_t> _lastInputLengths;
};

List<String> GetAllFunctionNames();