#pragma once
#include "DataContext.h"
#include "StrategyNode.h"
#include "Nodes/ExecutionPlan.h"

class PortfolioNode : public QNode {
public:
    RegistClassName(PortfolioNode);

    static const nlohmann::json getParams();

    PortfolioNode(Server* server);
    ~PortfolioNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual bool Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;

private:
    ExecutionPlan generatePlan(const Vector<symbol_t>& symbols,
                              const Vector<TradeAction>& actions,
                              double capital);
    ExecutionPlan generatePlan(DataContext& context, const Vector<symbol_t>& symbols,
                              const Vector<TradeAction>& actions,
                              double capital);
    // 根据运行模式获取可用资金
    double getAvailableCapital();
    bool isPlanChanged(const ExecutionPlan& newPlan);

private:
    Server*              _server;
    double               _positionRatio;      // 仓位比例 (0.0~1.0)
    Vector<symbol_t>     _pool;              // 交易池
    double               _initialCapital;     // 配置的初始本金 (最低优先级)
    ExecutionPlan        _lastPlan;           // 上一次的执行计划
};
