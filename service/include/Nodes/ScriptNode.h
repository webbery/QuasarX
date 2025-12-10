#pragma once
#include "StrategyNode.h"
#include <boost/interprocess/shared_memory_object.hpp>

class ScriptNode: public QNode {
public:
    virtual bool Init(const nlohmann::json& config);
    virtual bool Process(const String& strategy, DataContext& context);

private:
    
};
