#pragma once
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
    void StrategyBuy(symbol_t symbol, const DataFeatures& features);

    void StrategySell();

private:
    nng_socket _real_recv;  // 实盘接收
    nng_socket _real_send;

    List<String> _simulations;

    ITransfer* _simulater_trans;

    Server* _server;
    BrokerSubSystem* _virtualSystem;

    // next operations for symbol
    Map<symbol_t, int> _operations;
};