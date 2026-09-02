#pragma once
#include "MarketTiming.h"
#include "json.hpp"

class Server;
enum class TradeAction: char;

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
    nlohmann::json SendSummaryEmail(const String& strategy);

private:
    Map<symbol_t, DecisionSnapshot> _decisions;  // per-symbol，同 symbol 只保留最终决策
};
