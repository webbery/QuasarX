#include "Bridge/HX/HXTrade.h"
#include "Bridge/exchange.h"
#include "Bridge/HX/HXExchange.h"

#define INIT_PROMISE(type) \
auto itr = _exchange->_promises.find(nRequestID);\
if (itr == _exchange->_promises.end())\
    return;\
PromisePtr<type> prom = static_pointer_cast<std::promise<type>>(_exchange->_promises[nRequestID]);\
_exchange->_promises.erase(nRequestID)

#define SET_PROMISE(value) prom->set_value(value);


template<typename T>
using PromisePtr = std::shared_ptr<std::promise<T>>;

namespace {
    OrderStatus toOrderStatus(char status) {
        switch (status)
        {
        case TORASTOCKAPI::TORA_TSTP_OST_Unknown:
            break;
        case TORASTOCKAPI::TORA_TSTP_OST_Accepted://交易所已接�?
            return OrderStatus::OrderAccept;
        case TORASTOCKAPI::TORA_TSTP_OST_PartTraded://部分成交
            return OrderStatus::OrderPartSuccess;
        case TORASTOCKAPI::TORA_TSTP_OST_AllTraded://全部成交
            return OrderStatus::OrderSuccess;
        case TORASTOCKAPI::TORA_TSTP_OST_PartTradeCanceled://部成部撤
            return OrderStatus::PartSuccessCancel;
        case TORASTOCKAPI::TORA_TSTP_OST_AllCanceled://全部撤单
            return OrderStatus::CancelSuccess;
        case TORASTOCKAPI::TORA_TSTP_OST_Rejected://交易所已拒�?
            return OrderStatus::OrderReject;
        case TORASTOCKAPI::TORA_TSTP_OST_SendTradeEngine://发往交易核心
            break;
        default:
            break;
        }
        return OrderStatus::OrderUnknow;
    }

    position_t toPosition(const TORASTOCKAPI::CTORATstpPositionField& field) {
        position_t position;
        position._symbol = to_symbol(field.SecurityID);
        position._name = String(field.SecurityName, strlen(field.SecurityName));
        position._holds = field.CurrentPosition;
        position._validHolds = field.AvailablePosition;
        position._price = field.HistoryPosPrice;
        return position;
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
    INIT_PROMISE(TORASTOCKAPI::CTORATstpRspUserLoginField);

    _exchange->_maxInsertOrder = pRspUserLoginField->OrderInsertCommFlux;
    _exchange->_maxTradeReq = pRspUserLoginField->TradeCommFlux;
    _exchange->_maxQuoteReq = pRspUserLoginField->QueryCommFlux;
    _exchange->_maxCancelOrder = pRspUserLoginField->OrderActionCommFlux;
    _exchange->_trader_login = true;
    _exchange->_login_status = true;
    SET_PROMISE(*pRspUserLoginField);
}

void HXTrade::OnRspQryShareholderAccount(TORASTOCKAPI::CTORATstpShareholderAccountField* pShareholderAccountField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID, bool bIsLast)
{
    INIT_PROMISE(String);
    if (pRspInfoField->ErrorID != 0) {
        FATAL("get shareholder account fail: {} {}", pRspInfoField->ErrorID, pRspInfoField->ErrorMsg);
        return;
    }
    String acc(pShareholderAccountField->ShareholderID, strlen(pShareholderAccountField->ShareholderID));
    SET_PROMISE(acc);
}

void HXTrade::OnRspError(TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast) {

}
void HXTrade::OnRspOrderInsert(TORASTOCKAPI::CTORATstpInputOrderField *pInputOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {
    order_id id{ static_cast<uint64_t>(nRequestID) };
    TradeReport report;
    if (pRspInfoField->ErrorID == 0) {// 交易系统已接收报�?
        _investor = pInputOrderField->InvestorID;
        LOG("Order {} accept", nRequestID);
        report._status = OrderStatus::OrderAccept;
        _exchange->OnOrderReport(id, report);
    }
    else {
        LOG("Order {} reject", nRequestID);
        // 交易系统拒绝报单
        report._status = OrderStatus::OrderReject;
        _exchange->OnOrderReport(id, report);
    }
}
void HXTrade::OnRtnOrder(TORASTOCKAPI::CTORATstpOrderField *pOrderField) {
    order_id id{ static_cast<uint64_t>(pOrderField->OrderRef) };
    TradeReport report;
    report._status = toOrderStatus(pOrderField->OrderStatus);
    if (report._status != OrderStatus::OrderUnknow) {
        _exchange->OnOrderReport(id, report);
    }
}

void HXTrade::OnErrRtnOrderInsert(TORASTOCKAPI::CTORATstpInputOrderField *pInputOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {

}

void HXTrade::OnRtnTrade(TORASTOCKAPI::CTORATstpTradeField *pTradeField) {
    order_id id{ static_cast<uint64_t>(pTradeField->OrderRef)};
    TradeReport report;
    report._status = OrderStatus::OrderSuccess;

    _exchange->OnOrderReport(id, report);
}

void HXTrade::OnRspOrderAction(TORASTOCKAPI::CTORATstpInputOrderActionField *pInputOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {
    order_id id{ static_cast<uint64_t>(pInputOrderActionField->OrderRef)};
    TradeReport report;
    if (pRspInfoField && pRspInfoField->ErrorID == 0) {
        // 取消成功
        report._status = OrderStatus::CancelSuccess;
        _exchange->OnOrderReport(id, report);
    } else {
        report._status = OrderStatus::CancelFail;
        _exchange->OnOrderReport(id, report);
    }
}

void HXTrade::OnErrRtnOrderAction(TORASTOCKAPI::CTORATstpInputOrderActionField* pInputOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID)
{
    TradeReport report;
    report._status = OrderStatus::CancelFail;
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
    if (pOrderField && pRspInfoField->ErrorID == 0) {
        Order order;
        order._status = toOrderStatus(pOrderField->OrderStatus);
        order._side = pOrderField->Direction;
        order._volume = pOrderField->VolumeTraded;
        order._order[0]._price = pOrderField->LimitPrice;
        order._symbol = to_symbol(pOrderField->SecurityID);
        _orders.emplace_back(std::move(order));
        if (bIsLast) {
            prom->set_value(*pOrderField);
        }
    }
    else {
        LOG("query order fail");
        if (bIsLast) {
            prom->set_exception(std::current_exception());
        }
    }
}

void HXTrade::OnRspQryPosition(TORASTOCKAPI::CTORATstpPositionField *pPositionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast) {
    if (bIsLast) {
        INIT_PROMISE(bool);
        if (pRspInfoField->ErrorID != 0) {
            prom->set_exception(std::current_exception());
            return;
        }
        if (pPositionField) {
            position_t position = toPosition(*pPositionField);
            _positions.emplace_back(std::move(position));
        }
        SET_PROMISE(true);
    } else {
        if (pRspInfoField->ErrorID != 0) {
            return;
        }
        position_t position = toPosition(*pPositionField);
        _positions.emplace_back(std::move(position));
    }
    
}
    
void HXTrade::OnRspQryTradingFee(TORASTOCKAPI::CTORATstpTradingFeeField *pTradingFeeField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast) {

}
    
void HXTrade::OnRspQryInvestorTradingFee(TORASTOCKAPI::CTORATstpInvestorTradingFeeField *pInvestorTradingFeeField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast) {
    if (pRspInfoField && pRspInfoField->ErrorID == 0) {
        if (!bIsLast) {

        } else {
            INIT_PROMISE(TORASTOCKAPI::CTORATstpInvestorTradingFeeField);
            if (!pInvestorTradingFeeField) {
                LOG("get commission fail");
                return;
            }
            SET_PROMISE(*pInvestorTradingFeeField);
        }
    }
} 