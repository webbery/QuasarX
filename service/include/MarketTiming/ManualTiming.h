#pragma once
#include "MarketTiming.h"
#include "json.hpp"
#include "RiskContext.h"

class Server;
enum class TradeAction: char;
struct EodDebugReport;

// 决策快照（ManualTiming 内部使用）
struct DecisionSnapshot {
    symbol_t _symbol;
    TradeAction _action;
    int64_t _quantity = 0;
    double _price = 0.0;
    int _flag = 0;
    int _epoch = -1;
    bool _signalEvaluated = false;  // true=SignalNode 评估产生的；false=ExecuteNode 默认补 HOLD
};

// 紧急平仓记录（日终策略执行时由ExecuteNode记录）
struct EmergencyCloseRecord {
    symbol_t _symbol;
    RiskTriggerType _triggerType = RiskTriggerType::None;
    double _cost = 0.0;
    double _currentPrice = 0.0;
    int64_t _quantity = 0;
};

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

    // 发送汇总邮件（含 SSE），返回非 HOLD 决策数组（action=BUY/SELL，兼容 DailyDecisionJson::parseAction），清空累积器
    // strategyCapital: 从 flow._capital 传入，当 CapitalPool 未注册时作为 fallback 显示资金占比
    // portfolioValue: 今日组合市值（broker 持仓 × 最新收盘价），0 表示不展示
    // debugReport: 可选的调试报告（含附件、HTML 片段），非空时附加到邮件
    nlohmann::json SendSummaryEmail(const String& strategy, double strategyCapital = 0.0,
                                    double portfolioValue = 0.0,
                                    const EodDebugReport* debugReport = nullptr);

    // 访问当前累积的决策（SendSummaryEmail 调用前可用）
    const Map<symbol_t, DecisionSnapshot>& getDecisions() const { return _decisions; }
    
    // 紧急平仓记录管理（由ExecuteNode在日终策略执行时调用）
    void AddEmergencyClose(symbol_t sym, RiskTriggerType type, double cost, double current, int64_t qty) {
        _emergencyCloses.push_back({sym, type, cost, current, qty});
    }
    const Vector<EmergencyCloseRecord>& GetEmergencyCloses() const { return _emergencyCloses; }
    void ClearEmergencyCloses() { _emergencyCloses.clear(); }

private:
    Map<symbol_t, DecisionSnapshot> _decisions;  // per-symbol，同 symbol 只保留最终决策
    Vector<EmergencyCloseRecord> _emergencyCloses;  // 紧急平仓记录（日终策略用）
};
