#pragma once
#include "StrategyNode.h"


class LSTMNode: public ArtificialIntelligenceNode {
public:
    RegistClassName(LSTMNode);
    
    ~LSTMNode();

    virtual bool Init(const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);

    static const nlohmann::json getParams();
private:
    
    
private:
    char _predictWindow = 1;
    char _inputWindow = 4;
    List<String> _inputNames;

};