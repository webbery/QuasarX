#pragma once
#include "DataContext.h"
#include "StrategyNode.h"
#include "Nodes/ExecutionPlan.h"
#include "RiskContext.h"

class BacktestContext;
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
    ExecutionPlan generatePlan(const String& strategy, DataContext& context, const Map<symbol_t, TradeAction>& decisions,
                              double capital);
    ExecutionPlan generatePlan(DataContext& context, const Map<symbol_t, TradeAction>& decisions,
                              double capital,
                              BacktestContext* btContext);
    bool isPlanChanged(const ExecutionPlan& newPlan);

    // ── 仓位 sizing 方法（默认关闭，保持原有等权行为） ──
    enum class SizingMethod {
        Equal,              // 默认：等权分配
        Kelly,              // Kelly 公式
        VolatilityTarget,   // 目标波动率反推
        Strength            // 信号强度加权（读取 {symbol}.strength → 归一化权重）
    };
    SizingMethod _sizing_method = SizingMethod::Equal;
    double _max_single_pct = 1.0;     // 单标的上限（默认不限制）
    double _max_total_pct = 1.0;      // 总仓位上限（默认不限制）
    double _vol_target = 0.02;        // 日波动目标（VolatilityTarget 模式用）

    // Strength 模式：归一化后的权重（applySizingWeights 计算，generatePlan 使用）
    Map<symbol_t, double> _weights;

    // 风控短路检查：RiskContext.triggered 为 true 时，将 decisions 全部改为 SELL
    void applyRiskContext(DataContext& context, Map<symbol_t, TradeAction>& decisions);
    // 按 sizing_method 调整仓位权重
    void applySizingWeights(DataContext& context, Map<symbol_t, TradeAction>& decisions, double targetCapital);

private:
    Server*              _server;
    double               _positionRatio;      // 仓位比例 (0.0~1.0)
    Set<symbol_t>        _pool;              // 交易池（自动去重）
    ExecutionPlan        _lastPlan;           // 上一次的执行计划
    bool                 _allowShort = false; // 是否允许做空
};
