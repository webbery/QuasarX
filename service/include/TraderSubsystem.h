#pragma once
#include "std_header.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Transfer.h"
#include <mutex>
#include <variant>

enum class OrderType: char;
class Server;
class BrokerSubSystem;
class RiskSubSystem;

/**
 * 
 */
class TraderSystem {
public:
    TraderSystem(Server* handle, const String& dbpath);

    ~TraderSystem();

    void SetupSimulation(const String& name);

    void Start();

    void RegistCollection(const String& name);
    void ClearCollections();

    const std::variant<float, Vector<float>>& GetCollection(const String& name) const;

private:
    bool DailyBuy(symbol_t symbol, const DataFeatures& features);

    bool ImmediatelyBuy(symbol_t symbol, double price, OrderType type);

    bool StrategySell(symbol_t symbol, const DataFeatures& features);
    bool ImmediatelySell(symbol_t symbol, double price, OrderType type);

    bool StrategyShort(symbol_t symbol, const DataFeatures& features);

    bool IsNearClose(symbol_t);

private:
    RiskSubSystem* _riskSystem;
    nng_socket _real_recv;  // 实盘接收
    nng_socket _real_send;

    List<String> _simulations;

    ITransfer* _simulater_trans;

    Server* _server;
    BrokerSubSystem* _broker;

    // next operations for symbol
    Map<symbol_t, int> _operations;

    Set<time_range> _stock_working_range;

    std::mutex _mtx;
    Map<size_t, std::variant<float, Vector<float>>> _collections;
};