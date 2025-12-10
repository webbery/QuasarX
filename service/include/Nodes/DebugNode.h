#pragma once
#include "StrategyNode.h"

class DebugNode: public QNode {
public:
    virtual bool Init(const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);
};
