#pragma once
#include "StrategyNode.h"
#include "Function/Function.h"

/**
 * @brief 用于测试策略流程,使用R^2指标及
 */
class TestNode: public QNode {
public:
    RegistClassName(SignalNode);
    /**
     * @brief 获取节点的可用配置参数信息
     */
    static const nlohmann::json getParams();

    TestNode(Server* server);
    ~TestNode();

    virtual bool Init(const nlohmann::json& config);
    virtual bool Process(const String& strategy, DataContext& context);

private:
    Set<String> _input_keys;

    R2* _pR2 = nullptr;
};
