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
    Manual,               // 决策型：策略只产意图，不实际下单
    Shadow,               // 影子模式（未配置 type 时的默认值）
};

class ExecuteNode: public QNode {
public:
    ExecuteNode(Server* server);
    virtual ~ExecuteNode();
    virtual bool Init(const nlohmann::json& config);
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;

    virtual void Prepare(const String& strategy, DataContext& context);

    const List<Pair<symbol_t, TradeReport>>& GetReports() const;

    ExecuteType GetExecType() const { return _execType; }
    ITimingStrategy* GetTiming() const { return _timing; }

private:
    ITimingStrategy* GenerateTiming(ExecuteType type);
private:
    Server* _server;
    ITimingStrategy* _timing = nullptr;
    ExecuteType _execType = ExecuteType::Shadow;
};
