#pragma once
#include "StrategyNode.h"

class ExecuteNode: public QNode {
public:
    ExecuteNode(Server* );
    virtual bool Init(const nlohmann::json& config);
    virtual bool Process(const String& strategy, DataContext& context);
};
