#pragma once
#include "StrategyNode.h"
#include "server.h"

class QuoteInputNode;

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
    // 全部上游 input 节点：降频视图下取它们的定稿计数（GetViewVersion）判断
    // 「是否出现了新的低频 bar」。不能用输入向量长度判断——视图序列在日线
    // 网格上逐日前向填充，长度每个 epoch 都在增长。
    Vector<QuoteInputNode*> _upstreamQuotes;
    // 每个 symbol 上一次调用 callable 时看到的定稿计数
    Map<String, uint64_t> _lastViewVersion;
};

List<String> GetAllFunctionNames();