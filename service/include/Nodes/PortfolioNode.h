#pragma once
#include "DataContext.h"
#include "StrategyNode.h"
#include "Nodes/ExecutionPlan.h"
#include "RiskContext.h"

#include <memory>

class BacktestContext;

// 组合计算模型（等权 / Kelly / 目标波动率 / 强度加权 / MVO）通过基类指针
// 持有：Init 时按 sizing_method 只创建被选中的那一个，未选中的模型及其
// 递推状态都不占内存。
namespace portfolio {
class PortfolioModel;
}

class PortfolioNode : public QNode {
public:
    RegistClassName(PortfolioNode);

    static const nlohmann::json getParams();

    PortfolioNode(Server* server);
    ~PortfolioNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual void Prepare(const String& strategy, DataContext& context)override;
    virtual Map<String, ArgType> out_elements() override;

private:
    // 合并后的 generatePlan：通过 adapter 注入持仓与价格来源
    ExecutionPlan generatePlan(
        const String& strategy,
        DataContext& context,
        const Map<symbol_t, TradeAction>& decisions,
        double targetCapital,
        std::function<double(symbol_t)> getQuotePrice,
        std::function<int64_t(symbol_t)> getPositionQuantity,
        const String& source);
    bool isPlanChanged(const ExecutionPlan& newPlan);

    // 风控短路检查：RiskContext.triggered 为 true 时，将 decisions 全部改为 SELL
    void applyRiskContext(DataContext& context, Map<symbol_t, TradeAction>& decisions);

    // 按当前的组合计算模型调整仓位权重
    void applySizingWeights(const String& strategy,
                            DataContext& context,
                            Map<symbol_t, TradeAction>& decisions,
                            double targetCapital,
                            const std::function<double(symbol_t)>& getQuotePrice,
                            const std::function<int64_t(symbol_t)>& getPositionQuantity);

private:
    Server*                                  _server;
    double                                   _positionRatio;   // 仓位比例 (0.0~1.0)
    Set<symbol_t>                            _pool;            // 交易池（自动去重）
    ExecutionPlan                            _lastPlan;        // 上一次的执行计划
    bool                                     _allowShort = false; // 是否允许做空

    // 组合计算模型（Init 时创建，只保留被选中的那一个）
    std::unique_ptr<portfolio::PortfolioModel> _sizing_model;

    // 模型算出的权重（占 targetCapital 的比例），generatePlan 使用。
    // 空 map = 不调整，按等权均分。
    Map<symbol_t, double> _weights;
};
