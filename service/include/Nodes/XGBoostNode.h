#pragma once
#include "StrategyNode.h"
#include "std_header.h"
#include <xgboost/c_api.h>
#include <xgboost/version_config.h>

/**
 * XGBoost 推理节点
 *
 * 加载预训练的 XGBoost 模型文件，对输入特征做推理，输出预测结果。
 *
 * 参数：
 *   modelFile    — 模型文件路径（.json / .ubj）
 *   features     — 逗号分隔的特征名列表（对应 context 中的 key）
 *   objective    — 目标函数类型（binary:logistic / multi:softprob / reg:squarederror）
 *   num_class    — 分类数（多分类时 > 2）
 *
 * 输出（按 objective 动态生成）：
 *   binary:logistic  → xgb_prob_0, xgb_prob_1
 *   multi:softprob   → xgb_prob_0, ..., xgb_prob_{n-1}
 *   reg:squarederror → xgb_prediction
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

private:
    Server* _server;
    String _label;

    // 配置参数
    String _model_file;
    String _objective;
    Vector<String> _feature_keys;
    int _num_class = 1;

    // XGBoost 句柄
    BoosterHandle _booster = nullptr;
    int _n_features = 0;
    bool _loaded = false;

    // 输出声明
    Map<String, ArgType> _outputs;

    void buildOutputs();
    void cleanup();
};
