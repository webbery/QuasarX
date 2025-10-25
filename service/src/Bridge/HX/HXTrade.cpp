#include "Bridge/HX/HXTrade.h"
#include "Bridge/exchange.h"
#include "Bridge/HX/HXExchange.h"

template<typename T>
using PromisePtr = std::shared_ptr<std::promise<T>>;

namespace {
    OrderStatus toOrderStatus(char status) {
        switch (status)
        {
        case TORASTOCKAPI::TORA_TSTP_OST_Unknown:
            break;
        case TORASTOCKAPI::TORA_TSTP_OST_Accepted://交易所已接收
            return OrderStatus::OrderAccept;
        case TORASTOCKAPI::TORA_TSTP_OST_PartTraded://部分成交
            return OrderStatus::OrderPartSuccess;
        case TORASTOCKAPI::TORA_TSTP_OST_AllTraded://全部成交
            return OrderStatus::OrderSuccess;
        case TORASTOCKAPI::TORA_TSTP_OST_PartTradeCanceled://部成部撤
            return OrderStatus::PartSuccessCancel;
        case TORASTOCKAPI::TORA_TSTP_OST_AllCanceled://全部撤单
            return OrderStatus::CancelSuccess;
        case TORASTOCKAPI::TORA_TSTP_OST_Rejected://交易所已拒绝
            return OrderStatus::OrderReject;
        case TORASTOCKAPI::TORA_TSTP_OST_SendTradeEngine://发往交易核心
            break;
        default:
            break;
        }
        return OrderStatus::OrderUnknow;
    }
}
HXTrade::HXTrade(HXExchange* exc):_exchange(exc) {

}

void HXTrade::OnRspUserLogin(TORASTOCKAPI::CTORATstpRspUserLoginField* pRspUserLoginField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID)
{
    if (pRspInfoField->ErrorID != 0) {
        FATAL("trader login fail: {} {}", pRspInfoField->ErrorID, pRspInfoField->ErrorMsg);
        return;
    }
    _exchange->_maxInsertOrder = pRspUserLoginField->OrderInsertCommFlux;
    _exchange->_maxTradeReq = pRspUserLoginField->TradeCommFlux;
    _exchange->_maxQuoteReq = pRspUserLoginField->QueryCommFlux;
    _exchange->_maxCancelOrder = pRspUserLoginField->OrderActionCommFlux;
    _exchange->_trader_login = true;
    _exchange->_login_status = true; 
}

void HXTrade::OnRspError(TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast) {

}
void HXTrade::OnRspOrderInsert(TORASTOCKAPI::CTORATstpInputOrderField *pInputOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {
    order_id id{ nRequestID };
    TradeReport report;
    if (pRspInfoField->ErrorID == 0) {// 交易系统已接收报单
        report._status = OrderStatus::OrderAccept;
        _exchange->OnOrderReport(id, report);
    }
    else {
        // 交易系统拒绝报单
        report._status = OrderStatus::OrderReject;
        _exchange->OnOrderReport(id, report);
    }
}
void HXTrade::OnRtnOrder(TORASTOCKAPI::CTORATstpOrderField *pOrderField) {
    order_id id{ pOrderField->OrderRef };
    TradeReport report;
    report._status = toOrderStatus(pOrderField->OrderStatus);
    if (report._status != OrderStatus::OrderUnknow) {
        _exchange->OnOrderReport(id, report);
    }
}

void HXTrade::OnErrRtnOrderInsert(TORASTOCKAPI::CTORATstpInputOrderField *pInputOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {

}

void HXTrade::OnRtnTrade(TORASTOCKAPI::CTORATstpTradeField *pTradeField) {
    order_id id{ pTradeField->OrderRef};
    TradeReport report;
    report._status = OrderStatus::CancelPartSuccess;

    _exchange->OnOrderReport(id, report);
}

void HXTrade::OnRspOrderAction(TORASTOCKAPI::CTORATstpInputOrderActionField *pInputOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {

}

void HXTrade::OnErrRtnOrderAction(TORASTOCKAPI::CTORATstpInputOrderActionField* pInputOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID)
{

}

void HXTrade::OnRspCondOrderInsert(TORASTOCKAPI::CTORATstpInputCondOrderField *pInputCondOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {

}

void HXTrade::OnRtnCondOrder(TORASTOCKAPI::CTORATstpConditionOrderField *pConditionOrderField) {

}

void HXTrade::OnErrRtnCondOrderInsert(TORASTOCKAPI::CTORATstpInputCondOrderField *pInputCondOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {

}

void HXTrade::OnRspCondOrderAction(TORASTOCKAPI::CTORATstpInputCondOrderActionField *pInputCondOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {

}

void HXTrade::OnErrRtnCondOrderAction(TORASTOCKAPI::CTORATstpInputCondOrderActionField *pInputCondOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {

}

void HXTrade::OnRspQryTradingAccount(TORASTOCKAPI::CTORATstpTradingAccountField* pTradingAccountField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID, bool bIsLast)
{
    if (pRspInfoField->ErrorID == 0 && pTradingAccountField) {
        PromisePtr<TORASTOCKAPI::CTORATstpTradingAccountField> prom = static_pointer_cast<std::promise<TORASTOCKAPI::CTORATstpTradingAccountField>>(_exchange->_promises[nRequestID]);
        _exchange->_promises.erase(nRequestID);
        prom->set_value(*pTradingAccountField);
    }
}

void HXTrade::OnRspQryOrder(TORASTOCKAPI::CTORATstpOrderField* pOrderField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID, bool bIsLast)
{
    PromisePtr<TORASTOCKAPI::CTORATstpOrderField> prom = static_pointer_cast<std::promise<TORASTOCKAPI::CTORATstpOrderField>>(_exchange->_promises[nRequestID]);
    _exchange->_promises.erase(nRequestID);
    if (pRspInfoField->ErrorID == 0) {
        Order order;
        order._status = toOrderStatus(pOrderField->OrderStatus);
        order._side = pOrderField->Direction;
        order._volume = pOrderField->VolumeTraded;
        order._order[0]._price = pOrderField->LimitPrice;
        _orders.emplace_back(std::move(order));
        if (bIsLast) {
            prom->set_value(*pOrderField);
        }
    }
    else {
        LOG("query order fail");
        prom->set_exception(std::current_exception());
    }
}
