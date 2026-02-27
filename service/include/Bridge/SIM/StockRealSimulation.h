#pragma once
#include "Bridge/HX/HXExchange.h"

/**
 * 实盘仿真，任何下单都只记录到数据库，不会真正提交。用于实盘测试环境
 */
class StockRealSimulation: public HXExchange {
public:
    StockRealSimulation(Server* server);
    ~StockRealSimulation();

    virtual order_id AddOrder(const symbol_t& symbol, OrderContext* order);
    virtual Boolean CancelOrder(order_id id, OrderContext* order);
    virtual void OnOrderReport(order_id id, const TradeReport& report);
    virtual bool GetOrders(SecurityType type, OrderList& ol);
    virtual bool GetOrder(const String& sysID, Order& ol);
};
