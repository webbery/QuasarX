#pragma once 
#include "Bridge/exchange.h"

class TradeSignal;
class DataContext;
class Server;
class ITimingStrategy {
public:
    ITimingStrategy(Server* server): _server(server) {}
    virtual ~ITimingStrategy(){}

    virtual bool processSignal(const String& strategy, const TradeSignal& signal, const DataContext& context) = 0;

    const List<Pair<symbol_t, TradeReport>>& GetReports() const {
        return _reports;
    }
protected:
    Server* _server;
    List<Pair<symbol_t, TradeReport>> _reports;
};
