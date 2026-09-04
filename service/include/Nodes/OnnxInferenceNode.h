#pragma once
#include "StrategyNode.h"
#include "std_header.h"
#include "onnxruntime/onnxruntime_cxx_api.h"

enum class OnnxObjective {
    Regression,
    Binary,
    MultiClass,
};

/**
 * 通用 ONNX 推理节点
 *
 * 加载任意 ONNX 模型（KAN / Transformer / NARX 等），自动从模型 metadata 推导
 * lookback 和 outputDim，per-symbol 特征缓冲 + 滑动窗口推理。
 *
 * 参数：
 *   modelFile    — ONNX 模型路径（.onnx），解析到 {dbPath}/models/ 下
 *   features     — 逗号分隔特征逻辑名（顺序必须与训练一致）
 *   numThreads   — 推理线程数（默认 hardware_concurrency/2）
 *   arIndex      — (可选) autoregressive 反馈的特征索引（NARX 用）
 *
 * 输出（per-symbol）：
 *   regression   → {sym}.onnx_pred
 *   binary       → {sym}.onnx_probs_0, {sym}.onnx_probs_1
 *   multiclass   → {sym}.onnx_probs_0..N-1, {sym}.onnx_prediction
 */
class OnnxInferenceNode : public QNode {
public:
    static const nlohmann::json getParams();
    RegistClassName(OnnxInferenceNode);

    OnnxInferenceNode(Server* server);
    ~OnnxInferenceNode();

    bool Init(const nlohmann::json& config) override;
    NodeProcessResult Process(const String& strategy, DataContext& context) override;
    Map<String, ArgType> out_elements() override;
    void UpdateLabel(const String& label) override;

private:
    Server* _server;
    String _label;

    // 配置参数
    String _modelFile;
    Vector<String> _featureKeys;
    int _numThreads = 0;
    int _arIndex = -1;

    // ONNX Runtime
    Ort::Env _env;
    std::unique_ptr<Ort::Session> _session;
    Ort::SessionOptions _sessionOptions;
    Ort::AllocatorWithDefaultOptions _allocator;

    // 模型 metadata（从 shape 自动推导）
    int _nFeatures = 0;
    int _lookback = 1;
    int _outputDim = 1;
    OnnxObjective _objective = OnnxObjective::Regression;
    std::vector<int64_t> _inputShape;
    std::vector<int64_t> _outputShape;

    // tensor 名（持有 string 生命周期，供 char* 引用）
    std::vector<std::string> _inputNames;
    std::vector<std::string> _outputNames;
    std::vector<const char*> _inputPtrs;
    std::vector<const char*> _outputPtrs;

    // per-symbol 特征缓冲（滑动窗口）
    Map<String, std::deque<std::vector<float>>> _symbolBuffers;

    // 输出声明
    Map<String, ArgType> _outputs;
    Map<String, Vector<String>> _resolvedFeatures;

    int _consecutiveSkipCount = 0;
    String _lastSkipReason;

    static OnnxObjective inferObjective(int outputDim);
    void buildOutputs(const String& symbolPrefix);
    void cleanup();
};
