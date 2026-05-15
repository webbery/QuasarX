#pragma once
#include "StrategyNode.h"
#include "std_header.h"

/**
 * EMD 经验模态分解节点（信号处理）
 *
 * 将非平稳时间序列分解为多个本征模态函数（IMF），
 * 适用于趋势提取、降噪和多尺度分析。
 *
 * 输入：timeseries（如 close 价格序列）
 * 输出：label.IMF_0, label.IMF_1, ...（多个 IMF 分量时间序列）
 */
class EMDNode : public QNode {
public:
    RegistClassName(EMDNode);
    EMDNode(Server* server);

    static const nlohmann::json getParams();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;
    virtual void UpdateLabel(const String& label) override;

private:
    Server* _server;
    String _label;
    int _numIMFs;                  // IMF 分量数量
    Map<String, ArgType> _params;  // 输入参数（来自上游节点的输出）
    Map<String, ArgType> _outputs; // 输出元素声明
};
