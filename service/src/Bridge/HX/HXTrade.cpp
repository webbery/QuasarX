#include "Bridge/HX/HXTrade.h"
#include "Bridge/exchange.h"
#include "Bridge/HX/HXExchange.h"
#include "Util/string_algorithm.h"
#include "Bridge/OrderLimit.h"

namespace {
    OrderStatus toOrderStatus(char status) {
        switch (status)
        {
        case TORASTOCKAPI::TORA_TSTP_OST_Unknown:
            break;
        case TORASTOCKAPI::TORA_TSTP_OST_Accepted://
            return OrderStatus::OrderAccept;
        case TORASTOCKAPI::TORA_TSTP_OST_PartTraded://
            return OrderStatus::OrderPartSuccess;
        case TORASTOCKAPI::TORA_TSTP_OST_AllTraded://
            return OrderStatus::OrderSuccess;
        case TORASTOCKAPI::TORA_TSTP_OST_PartTradeCanceled://
            return OrderStatus::PartSuccessCancel;
        case TORASTOCKAPI::TORA_TSTP_OST_AllCanceled://
            return OrderStatus::CancelSuccess;
        case TORASTOCKAPI::TORA_TSTP_OST_Rejected://
            return OrderStatus::OrderReject;
        case TORASTOCKAPI::TORA_TSTP_OST_SendTradeEngine://
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
        position._price = field.OpenPosCost / field.CurrentPosition;
        position._curPrice = 0;
        return position;
    }
}
HXTrade::HXTrade(HXExchange* exc):_exchange(exc) {

}

void HXTrade::OnFrontConnected()
{
    INFO("HX Stock connected");
}

void HXTrade::OnFrontDisconnected(int nReason) {
    INFO("HX stock disconnect:{}", nReason);
    _exchange->InitStockTrade();
    _exchange->_stock_login = false;
    _exchange->_login_status = false;
}

void HXTrade::OnRspUserLogin(TORASTOCKAPI::CTORATstpRspUserLoginField* pRspUserLoginField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID)
{
    if (pRspInfoField->ErrorID != 0) {
        FATAL("trader login fail: {} {}", pRspInfoField->ErrorID, to_utf8(pRspInfoField->ErrorMsg));
        return;
    }
    INIT_PROMISE(TORASTOCKAPI::CTORATstpRspUserLoginField);
    _exchange->Reset();
    // 单位时间最大突发流量20次请求
    _exchange->_stockHandle._cancelLimitation = pRspUserLoginField->OrderActionCommFlux;
    _exchange->_stockHandle._insertLimitation = pRspUserLoginField->OrderInsertCommFlux;
    _exchange->_stockHandle._insertLimit = new OrderLimit(pRspUserLoginField->OrderInsertCommFlux, pRspUserLoginField->OrderInsertCommFlux / 2);
    _exchange->_stockHandle._maxTradeReq = pRspUserLoginField->TradeCommFlux;
    _exchange->_stockHandle._maxQuoteReq = pRspUserLoginField->QueryCommFlux;
    _exchange->_stockHandle._cancelLimit = new OrderLimit(pRspUserLoginField->OrderActionCommFlux, pRspUserLoginField->OrderActionCommFlux / 2);
    _exchange->_stock_login = true;
    _exchange->_login_status = true;
    SET_PROMISE(*pRspUserLoginField);
    INFO("Stock Logined");
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
    LOG("OnRspError {}: {}", pRspInfoField->ErrorID, to_utf8(pRspInfoField->ErrorMsg));
}

void HXTrade::OnRspOrderInsert(TORASTOCKAPI::CTORATstpInputOrderField *pInputOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {
    order_id id;
    id._id = nRequestID;
    TradeReport report;
    report._type = pInputOrderField->IInfo;
    memcpy(&report._time, pInputOrderField->SInfo, sizeof(time_t));
    report._price = pInputOrderField->LimitPrice;
    report._quantity = pInputOrderField->VolumeTotalOriginal;
    report._sysID = pInputOrderField->OrderSysID;
    report._side = pInputOrderField->Direction - 48;
    if (pRspInfoField->ErrorID == 0) {
        _investor = pInputOrderField->InvestorID;
        LOG("Order {}  code {} raccept", nRequestID, pInputOrderField->SecurityID);
        report._status = OrderStatus::OrderAccept;
        _exchange->OnOrderReport(id, report);
    }
    else {
        LOG("Order {}, code {} reject: {}", nRequestID, pInputOrderField->SecurityID, to_utf8(pRspInfoField->ErrorMsg));
        if (pRspInfoField->ErrorID == 341) {
            report._status = OrderStatus::PrivilegeReject;
        }
        else {
            report._status = OrderStatus::OrderReject;
        }
        _exchange->OnOrderReport(id, report);
    }
}
void HXTrade::OnRtnOrder(TORASTOCKAPI::CTORATstpOrderField *pOrderField) {
    order_id id;
    id._id = pOrderField->RequestID;
    TradeReport report;
    report._sysID = pOrderField->OrderSysID;
    report._status = toOrderStatus(pOrderField->OrderStatus);
    //INFO("order status: {}", (int)report._status);
    if (report._status != OrderStatus::OrderUnknow) {
        _exchange->OnOrderReport(id, report);
    }
}

void HXTrade::OnErrRtnOrderInsert(TORASTOCKAPI::CTORATstpInputOrderField *pInputOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {
    LOG("OnErrRtnOrderInsert {}: {}", pRspInfoField->ErrorID, to_utf8(pRspInfoField->ErrorMsg));
    order_id id;
    id._id = pInputOrderField->OrderRef;
    TradeReport report;
    if (pRspInfoField->ErrorID == 341) {
        report._status = OrderStatus::PrivilegeReject;
    }
    _exchange->OnOrderReport(id, report);
}

void HXTrade::OnRtnTrade(TORASTOCKAPI::CTORATstpTradeField *pTradeField) {
    order_id id;
    id._id = pTradeField->OrderRef;
    TradeReport report;
    report._status = OrderStatus::OrderSuccess;
    LOG("OnRtnTrade {} success", id._id);
    _exchange->OnOrderReport(id, report);
}

void HXTrade::OnRspOrderAction(TORASTOCKAPI::CTORATstpInputOrderActionField *pInputOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {
    order_id id;
    strcpy(id._sysID, pInputOrderActionField->SInfo);
    id._id = pInputOrderActionField->IInfo;
    TradeReport report;
    if (pRspInfoField && pRspInfoField->ErrorID == 0) {
        report._status = OrderStatus::CancelSuccess;
    } else {
        report._status = OrderStatus::CancelFail;
    }
    _exchange->OnOrderReport(id, report);
    INFO("OnRspOrderAction");
}

void HXTrade::OnErrRtnOrderAction(TORASTOCKAPI::CTORATstpInputOrderActionField* pInputOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID)
{
    TradeReport report;
    report._status = OrderStatus::CancelFail;
    order_id id;
    strcpy(id._sysID, pInputOrderActionField->SInfo);
    id._id = pInputOrderActionField->IInfo;
    _exchange->OnOrderReport(id, report);
    INFO("OnErrRtnOrderAction");
}

void HXTrade::OnRspCondOrderInsert(TORASTOCKAPI::CTORATstpInputCondOrderField *pInputCondOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {

}

void HXTrade::OnRtnCondOrder(TORASTOCKAPI::CTORATstpConditionOrderField *pConditionOrderField) {

}

void HXTrade::OnErrRtnCondOrderInsert(TORASTOCKAPI::CTORATstpInputCondOrderField *pInputCondOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {
    INFO("OnErrRtnCondOrderInsert");
}

void HXTrade::OnRspCondOrderAction(TORASTOCKAPI::CTORATstpInputCondOrderActionField *pInputCondOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {
    INFO("OnRspCondOrderAction");
}

void HXTrade::OnErrRtnCondOrderAction(TORASTOCKAPI::CTORATstpInputCondOrderActionField *pInputCondOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {
    INFO("OnErrRtnCondOrderAction");
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
    INIT_PROMISE(bool);
    if (pRspInfoField->ErrorID == 0) {
        if (pOrderField) {
            Order order;
            order._status = toOrderStatus(pOrderField->OrderStatus);
            order._side = pOrderField->Direction - 48;
            order._volume = pOrderField->VolumeTotalOriginal;
            order._order[0]._price = pOrderField->LimitPrice;
            order._symbol = to_symbol(pOrderField->SecurityID);
            order._id = pOrderField->RequestID;
            order._sysID = pOrderField->OrderSysID;
            memcpy(&order._time, pOrderField->SInfo, sizeof(time_t));
            order._type = (OrderType)pOrderField->IInfo;
            if (pOrderField->OrderType == TORASTOCKAPI::TORA_TSTP_ORDT_Normal) {
                // 
            }
            _orders.emplace_back(std::move(order));
        }
        if (bIsLast) {
            SET_PROMISE(true);
        }
    }
    else {
        LOG("query order fail");
        if (bIsLast) {
            SET_PROMISE(false);
        }
    }
    
}

void HXTrade::OnRspQryPosition(TORASTOCKAPI::CTORATstpPositionField *pPositionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast) {
    if (bIsLast) {
        INIT_PROMISE(bool);
        if (pRspInfoField->ErrorID != 0) {
            LOG("OnRspQryPosition {}: {}", pRspInfoField->ErrorID, pRspInfoField->ErrorMsg);
            prom->set_exception(std::make_exception_ptr(std::runtime_error("query position fail.")));
            return;
        }
        if (pPositionField) {
            position_t position = toPosition(*pPositionField);
            if (position._holds != 0) {
                _positions.emplace_back(std::move(position));
            }
        }
        SET_PROMISE(true);
    } else {
        if (pRspInfoField->ErrorID != 0) {
            LOG("OnRspQryPosition {}: {}", pRspInfoField->ErrorID, pRspInfoField->ErrorMsg);
            return;
        }
        position_t position = toPosition(*pPositionField);
        if (position._holds != 0) {
            _positions.emplace_back(std::move(position));
        }
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

void HXTrade::OnRspQryShareholderSpecPrivilege(TORASTOCKAPI::CTORATstpShareholderSpecPrivilegeField* pSpecPrivilegeField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID, bool bIsLast)
{
    INIT_PROMISE(bool);
    if (pRspInfoField && pRspInfoField->ErrorID != 0) {
        LOG("OnRspQryShareholderSpecPrivilege fail: {}", pRspInfoField->ErrorMsg);
        SET_PROMISE(false);
        return;
    }
    if (pSpecPrivilegeField) {
        INFO("MarketID {} {} , valid: {}", pSpecPrivilegeField->MarketID, pSpecPrivilegeField->SpecPrivilegeType, !pSpecPrivilegeField->bForbidden);
        if (!pSpecPrivilegeField->bForbidden) {
            _exchange->_stockHandle._privileges.insert(pSpecPrivilegeField->SpecPrivilegeType);
        }
    }
    if (bIsLast) {
        SET_PROMISE(true);
    }
}

void HXTrade::OnRspQrySecurity(TORASTOCKAPI::CTORATstpSecurityField* pSecurityField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID, bool bIsLast)
{
    INFO("OnRspQrySecurity");
    using info_t = std::tuple<char, int64_t, char>;
    INIT_PROMISE(info_t);
    info_t info;
    if (pRspInfoField && pRspInfoField->ErrorID == 0) {
        if (pSecurityField) {
            std::get<0>(info) = pSecurityField->MarketID;
            std::get<1>(info) = pSecurityField->SecurityStatus;
            std::get<2>(info) = pSecurityField->SecurityType;
        }
    }
    else {
    }
    SET_PROMISE(info);
}