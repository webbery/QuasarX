#pragma once
#include "Util/datetime.h"
#include "Util/system.h"
#include "std_header.h"
#include "Transfer.h"

class Server;
class BrokerSubSystem;
/**
 * 
 */
class TraderSystem {
public:
    TraderSystem(Server* handle, const String& dbpath);

    ~TraderSystem();

    void SetupSimulation(const String& name);

    void Start();

private:
    bool StrategyBuy(symbol_t symbol, const DataFeatures& features);

    bool StrategySell(symbol_t symbol, const DataFeatures& features);

    bool StrategyShort(symbol_t symbol, const DataFeatures& features);

    bool IsNearClose(symbol_t);
private:
    nng_socket _real_recv;  // 实盘接收
    nng_socket _real_send;

    List<String> _simulations;

    ITransfer* _simulater_trans;

    Server* _server;
    BrokerSubSystem* _virtualSystem;

    // next operations for symbol
    Map<symbol_t, int> _operations;

    Set<time_range> _stock_working_range;
};