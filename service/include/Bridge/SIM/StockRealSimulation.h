#pragma once
#include "Bridge/HX/HXExchange.h"
#include "Bridge/SIM/StockPositionManager.h"

/**
 * 实盘仿真，任何下单都只记录到数据库，不会真正提交。用于实盘测试环境
 */
class StockRealSimulation: public HXExchange {
public:
    StockRealSimulation(Server* server);
    ~StockRealSimulation();

    virtual const char* Name() override { return STOCK_REAL_SIM; }

    // 资金/持仓（模拟）
    virtual double GetAvailableFunds(run_id_t run_id) override;
    virtual AccountAsset GetAsset() override;
    virtual bool GetPosition(AccountPosition& pos) override;

    /**
     * @brief 提交订单（实盘仿真模式）
     * @param run_id 策略运行 ID，用于区分不同的策略实例
     * @param symbol 合约标的
     * @param order 订单上下文
     * @return 订单 ID
     */
    virtual order_id AddOrder(run_id_t run_id, const symbol_t& symbol, OrderContext* order) override;
    virtual Boolean CancelOrder(order_id id, OrderContext* order) override;
    virtual void OnOrderReport(order_id id, const TradeReport& report) override;
    virtual bool GetOrders(SecurityType type, OrderList& ol) override;
    virtual bool GetOrder(const String& sysID, Order& ol) override;

private:
    StockPositionManager _positionMgr;
};
