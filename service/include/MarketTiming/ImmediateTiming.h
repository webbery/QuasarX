#pragma once
#include "MarketTiming.h"

class ImmediateTiming: public ITimingStrategy {
public:
    ImmediateTiming(){}
    virtual ExecutionDecision processSignal(const TradeSignal& signal, const DataContext& context);
};
