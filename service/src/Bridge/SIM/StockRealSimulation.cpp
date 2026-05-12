#include "Bridge/SIM/StockRealSimulation.h"
#include "Bridge/HX/HXExchange.h"
#include "Bridge/SIM/BacktestContext.h"

StockRealSimulation::StockRealSimulation(Server* server)
    : HXExchange(server)
    , _positionMgr(BACKTEST_INITIAL_CAPITAL)
{
}

StockRealSimulation::~StockRealSimulation() {
}

double StockRealSimulation::GetAvailableFunds(run_id_t run_id) {
    return _positionMgr.GetAvailableFunds();
}

AccountAsset StockRealSimulation::GetAsset() {
    return _positionMgr.GetAsset();
}

bool StockRealSimulation::GetPosition(AccountPosition& pos) {
    return _positionMgr.GetPosition(pos);
}

order_id StockRealSimulation::AddOrder(run_id_t run_id, const symbol_t& symbol, OrderContext* order) {
    // 实盘仿真：更新持仓但不实际执行
    double price = order->_order._price;
    int64_t qty = order->_order._volume;
    if (order->_order._side == 0) {
        _positionMgr.Buy(symbol, qty, price);
    } else {
        _positionMgr.Sell(symbol, qty, price);
    }

    order_id id;
    return id;
}

Boolean StockRealSimulation::CancelOrder(order_id id, OrderContext* order) {
    return true;
}

void StockRealSimulation::OnOrderReport(order_id id, const TradeReport& report) {
    // 邮件通知等由基类处理
}

bool StockRealSimulation::GetOrders(SecurityType type, OrderList& ol) {
    return true;
}

bool StockRealSimulation::GetOrder(const String& sysID, Order& ol) {
    return true;
}
