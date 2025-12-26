#pragma once
#include "StrategyNode.h"

class ITimingStrategy;

enum class ExecuteType : char {
    ImmediatlyMarket,     // 立即执行(市价单)
    ImmediatlyLimit,      // 立即执行(限价单)
    VWAP,
    TWAP,
    Breakout,       // 突破入场
    LA,             // LiquidityAdaptive 流动性自适应
    MOC,
};

class ExecuteNode: public QNode {
public:
    ExecuteNode(Server* );
    virtual bool Init(const nlohmann::json& config);
    virtual bool Process(const String& strategy, DataContext& context);

    virtual void Prepare(const String& strategy, DataContext& context);

    const List<Pair<symbol_t, TradeReport>>& GetReports() const;

private:
    ITimingStrategy* GenerateTiming(ExecuteType type);
private:
    Server* _server;
    ITimingStrategy* _timing;
};
