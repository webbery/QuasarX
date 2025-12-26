#pragma once
#include "MarketTiming.h"

class Server;
class ImmediateTiming: public ITimingStrategy {
public:
    ImmediateTiming(Server* server, bool is_limit): _limit(is_limit), ITimingStrategy(server) {}
    virtual bool processSignal(const String& strategy, const TradeSignal& signal, const DataContext& context);

private:
    bool _limit;    // 是否限价单或者市价单
};
