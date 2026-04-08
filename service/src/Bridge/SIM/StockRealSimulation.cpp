#include "Bridge/SIM/StockRealSimulation.h"
#include "Bridge/HX/HXExchange.h"

StockRealSimulation::StockRealSimulation(Server* server)
:HXExchange(server) {

}

StockRealSimulation::~StockRealSimulation() {

}

order_id StockRealSimulation::AddOrder(uint16_t run_id, const symbol_t& symbol, OrderContext* order) {
    // run_id: 策略运行 ID，用于区分不同的策略实例（实盘仿真模式）
    order_id id;
    return id;
}

Boolean StockRealSimulation::CancelOrder(order_id id, OrderContext* order) {
    return true;
}

void StockRealSimulation::OnOrderReport(order_id id, const TradeReport& report) {

}

bool StockRealSimulation::GetOrders(SecurityType type, OrderList& ol) {

    return true;
}
bool StockRealSimulation::GetOrder(const String& sysID, Order& ol) {

    return true;
}