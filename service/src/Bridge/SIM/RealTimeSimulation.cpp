#include "Bridge/SIM/RealTimeSimulation.h"
#include "Bridge/HX/HXExchange.h"
#include "Bridge/SIM/BacktestContext.h"

RealTimeSimulation::RealTimeSimulation(Server* server)
    : HXExchange(server)
    , _positionMgr(BACKTEST_INITIAL_CAPITAL)
{
}

RealTimeSimulation::~RealTimeSimulation() {
}

double RealTimeSimulation::GetAvailableFunds(run_id_t run_id) {
    return _positionMgr.GetAvailableFunds();
}

AccountAsset RealTimeSimulation::GetAsset() {
    return _positionMgr.GetAsset();
}

bool RealTimeSimulation::GetPosition(AccountPosition& pos) {
    return _positionMgr.GetPosition(pos);
}

order_id RealTimeSimulation::AddOrder(run_id_t run_id, const symbol_t& symbol, OrderContext* order) {
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

Boolean RealTimeSimulation::CancelOrder(order_id id, OrderContext* order) {
    return true;
}

void RealTimeSimulation::OnOrderReport(order_id id, const TradeReport& report) {
    // 邮件通知等由基类处理
}

bool RealTimeSimulation::GetOrders(SecurityType type, OrderList& ol) {
    return true;
}

bool RealTimeSimulation::GetOrder(const String& sysID, Order& ol) {
    return true;
}
