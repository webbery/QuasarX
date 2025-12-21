#pragma once
#include "StrategyNode.h"

class ITimingStrategy;

enum class ExecuteType : char {
    Immediatly,     // 立即执行(市价单或者限价单)
};

class ExecuteNode: public QNode {
public:
    ExecuteNode(Server* );
    virtual bool Init(const nlohmann::json& config);
    virtual bool Process(const String& strategy, DataContext& context);

    virtual void Prepare(const String& strategy, DataContext& context);
private:
    ITimingStrategy* GenerateTiming(ExecuteType type);
private:
    Server* _server;
    ITimingStrategy* _timing;
};
