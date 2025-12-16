#pragma once
#include "StrategyNode.h"

class LogisticNode: public ArtificialIntelligenceNode {
public:
    static const nlohmann::json getParams();

    virtual bool Init(const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);
};
