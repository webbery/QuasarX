#pragma once
#include "std_header.h"
#include "Util/system.h"

enum class AgentType: char {
    Unknow,
    XGBoost,
    Count
};

struct Signal {
    symbol_t _symbol;
    int16_t _hold;  // positive/negtive
    YAS_DEFINE_STRUCT_SERIALIZE("Signal", _symbol, _hold);
};

class IAgent {
public:
    virtual ~IAgent() {}
    virtual int classify(const Vector<float>& data, short n_samples, short n_features, Vector<float>& result) = 0;
    virtual double predict() = 0;
    virtual void train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch) = 0;
};