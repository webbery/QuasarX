#pragma once
#include "StrategyNode.h"

class ITimingStrategy;

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
