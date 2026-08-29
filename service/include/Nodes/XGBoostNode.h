#pragma once
#include "StrategyNode.h"
#include "std_header.h"
#include <xgboost/c_api.h>
#include <xgboost/version_config.h>

enum class XGBObjective {
    BinaryLogistic,
    MultiSoftprob,
    MultiSoftmax,
    RegSquaredError,
};

/**
 * XGBoost 推理节点
 *
 * 加载预训练的 XGBoost 模型文件，对输入特征做推理，输出预测结果。
 * 支持多标的：从上游连接图自动发现 symbol，每个 symbol 独立推理。
 *
 * 参数：
 *   modelFile    — 模型文件路径（.json / .ubj）
 *   features     — 逗号分隔的特征逻辑名（如 "norm_ret,drift,breakout"）
 *   objective    — 目标函数（binary:logistic / multi:softprob / reg:squarederror）
 *   num_class    — 分类数（多分类时 > 2）
 *
 * 输出（带 symbol 前缀，按 objective 动态生成）：
 *   binary:logistic  → {symbol}.xgb_probs_0, {symbol}.xgb_probs_1
 *   multi:softprob   → {symbol}.xgb_probs_0, ..., {symbol}.xgb_probs_{n-1}
 *   reg:squarederror → {symbol}.xgb_prediction
 *
 * 公式中可用数组语法: xgb_probs[0] → xgb_probs_0
 */
class XGBoostNode : public QNode {
public:
    static const nlohmann::json getParams();
    RegistClassName(XGBoostNode);

public:
    XGBoostNode(Server* server);
    ~XGBoostNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;
    virtual void UpdateLabel(const String& label) override;

    const Vector<String>& featureKeys() const { return _feature_keys; }

private:
    Server* _server;
    String _label;

    // 配置参数
    String _model_file;
    XGBObjective _objective = XGBObjective::MultiSoftprob;
    Vector<String> _feature_keys;  // 逻辑名（如 "norm_ret", "drift"）
    int _num_class = 1;

    // XGBoost 句柄
    BoosterHandle _booster = nullptr;
    int _n_features = 0;
    bool _loaded = false;

    // 输出声明
    Map<String, ArgType> _outputs;

    // 按 symbol 解析的实际 context key（symbol → [contextKey per feature]）
    Map<String, Vector<String>> _resolved_features;

    // 连续 Skip 计数（用于区分预热期临时 Skip 和持续性故障）
    int _consecutiveSkipCount = 0;
    String _lastSkipReason;

    static XGBObjective parseObjective(const String& s);
    void buildOutputs(const String& symbolPrefix);
    void cleanup();
};
