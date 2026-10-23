#pragma once
#include "MarketTiming.h"
#include "json.hpp"
#include "RiskContext.h"
#include "Nodes/ProtectionNode.h"
#include "Util/DecisionSet.h"

class Server;
struct EodDebugReport;

/**
 * 决策型 Timing（ManualTiming）
 *
 * 用于 ExecuteType::Manual（默认）：策略只产意图，不实际下单。
 * 累积 per-symbol 最终决策（Map 覆盖），日终由 AgentSubSystem 调用 SendSummaryEmail。
 */
class ManualTiming : public ITimingStrategy {
public:
    ManualTiming(Server* server) : ITimingStrategy(server) {}
    virtual bool processSignal(const String& strategy, const TradeSignal& signal,
                               const DataContext& context) override;

    // 发送汇总邮件（含 SSE），清空累积器
    // 所有内容都从 DecisionSet 读：决策、资金、风控事件、组合市值都不再各自去摸
    // ManualTiming / StrategyCapital / ProtectionNode 的内部状态。
    // debugReport: 可选的调试报告（含附件、HTML 片段），非空时附加到邮件
    void SendSummaryEmail(const DecisionSet& set,
                          const EodDebugReport* debugReport = nullptr);

    // 仅推送 SSE（DecisionSet 驱动），供不需要邮件的场景使用
    void PushDecisionSse(const DecisionSet& set, const Map<symbol_t, int>& decisionIds);

    // 访问当前累积的决策（构建 DecisionSet 时用）
    const Map<symbol_t, DecisionSnapshot>& getDecisions() const { return _decisions; }

    // 访问本轮重放逐决策 bar 记录的持仓快照（构建 DecisionSet 时用）
    const Vector<DecisionSet::PositionSnapshot>& getPositionSnapshots() const { return _positionSnapshots; }

    // 清空本轮累积器（DecisionSet 消费完毕后调用）
    // 单独提供而不是在 SendSummaryEmail 末尾隐式清空：失败路径同样需要清，
    // 否则上一天的决策会漏进下一轮（10-22 那类"静默丢弃"的同源问题）。
    void ClearRun();

    // 风控事件（由 ProtectionNode 产生，构建 DecisionSet 时用）
    // 价格口径：原始价，与决策表里的成交价同口径
    void SetRiskEvents(const Vector<ProtectionNode::ProtectionEvent>& events) { _riskEvents = events; }
    const Vector<ProtectionNode::ProtectionEvent>& GetRiskEvents() const { return _riskEvents; }

private:
    // 逐决策 bar 记录持仓快照：daily_positions 的连续序列来源
    void recordPositionSnapshot(symbol_t symbol, TradeAction action, int64_t quantity,
                                const DataContext& context);

    Map<symbol_t, DecisionSnapshot> _decisions;  // per-symbol，同 symbol 只保留最终决策
    Vector<ProtectionNode::ProtectionEvent> _riskEvents;  // 风控触发事件（日终策略用）
    // 重放期间的运行中持仓 + 逐 bar 快照（只在本轮重放内有意义）
    Map<symbol_t, int64_t> _positions;
    Vector<DecisionSet::PositionSnapshot> _positionSnapshots;
};
