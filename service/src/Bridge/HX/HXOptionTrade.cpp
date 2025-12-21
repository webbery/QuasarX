#include "Bridge/HX/HXOptionTrade.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Bridge/HX/HXExchange.h"

namespace {
    OrderStatus toOrderStatus(char status) {
        switch (status)
        {
        case TORASPAPI::TORA_TSTP_SP_OST_PartTraded:
            return OrderStatus::OrderPartSuccess;
        case TORASPAPI::TORA_TSTP_SP_OST_Accepted://
            return OrderStatus::OrderAccept;
        case TORASPAPI::TORA_TSTP_SP_OST_AllTraded://
            return OrderStatus::OrderSuccess;
        case TORASPAPI::TORA_TSTP_SP_OST_PartTradedCancelled://
            return OrderStatus::PartSuccessCancel;
        case TORASPAPI::TORA_TSTP_SP_OST_Cancelled://
            return OrderStatus::CancelSuccess;
        case TORASPAPI::TORA_TSTP_SP_OST_Failed://
            return OrderStatus::OrderFail;
        case TORASPAPI::TORA_TSTP_SP_OST_Cached:
            return OrderStatus::OrderCached;
        case TORASPAPI::TORA_TSTP_SP_OST_SendTradeKernel://
            break;
        default:
            break;
        }
        return OrderStatus::OrderUnknow;
    }
}

HXOptionTrade::HXOptionTrade(HXExchange* exchange): _exchange(exchange) {

}

void HXOptionTrade::Release() {

}

void HXOptionTrade::OnFrontConnected()
{
    INFO("HX Option Connected.");
}

void HXOptionTrade::OnFrontDisconnected(int nReason) {
    INFO("HX Option Disconnected.");

}

void HXOptionTrade::OnRspUserLogin(TORASPAPI::CTORATstpSPRspUserLoginField* pRspUserLoginField, TORASPAPI::CTORATstpSPRspInfoField* pRspInfo, int nRequestID)
{
    if (pRspInfo->ErrorID != 0) {
        INFO("OnRspUserLogin {}.", to_utf8(pRspInfo->ErrorMsg));
        return;
    }
    INIT_PROMISE(TORASPAPI::CTORATstpSPRspUserLoginField);
    //_exchange->_optionHandle
    SET_PROMISE(*pRspUserLoginField);
    INFO("Option Logined");
}

void HXOptionTrade::OnRspError(TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    INFO("OnRspError {}.", to_utf8(pRspInfo->ErrorMsg));
}

void HXOptionTrade::OnRspOrderInsert(TORASPAPI::CTORATstpSPInputOrderField *pInputOrderField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID) {
    INFO("option insert order success");
}

void HXOptionTrade::OnRtnOrder(TORASPAPI::CTORATstpSPOrderField *pOrder) {
    INFO("option order success");
}

void HXOptionTrade::OnErrRtnOrderInsert(TORASPAPI::CTORATstpSPInputOrderField *pInputOrder, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo,int nRequestID) {

}

void HXOptionTrade::OnRspOrderAction(TORASPAPI::CTORATstpSPInputOrderActionField *pInputOrderActionField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID) {

}

void HXOptionTrade::OnRtnTrade(TORASPAPI::CTORATstpSPTradeField *pTrade) {
    INFO("option trade success");
}

void HXOptionTrade::OnRspExerciseInsert(TORASPAPI::CTORATstpSPInputExerciseField *pInputExerciseField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID) {
    INFO("option exercise isnert success");
}

void HXOptionTrade::OnRtnExercise(TORASPAPI::CTORATstpSPExerciseField *pExercise) {
    INFO("option exercise success");

}

void HXOptionTrade::OnRspExerciseAction(TORASPAPI::CTORATstpSPInputExerciseActionField *pInputExerciseActionField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID) {

}

void HXOptionTrade::OnRspQryOrder(TORASPAPI::CTORATstpSPOrderField *pOrder, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    INIT_PROMISE(bool);
    if (pRspInfo->ErrorID == 0) {
        if (pOrder) {
            Order order;
            order._side = (pOrder->Direction == TORASPAPI::TORA_TSTP_SP_D_Buy ? 0 : 1);
            order._sysID = pOrder->OrderSysID;
            order._status = toOrderStatus(pOrder->OrderStatus);
            _orders.emplace_back(std::move(order));
        }
        if (bIsLast) {
            SET_PROMISE(true);
        }
    }
    else {
        LOG("query option order fail: {}", pRspInfo->ErrorMsg);
        if (bIsLast) {
            SET_PROMISE(false);
        }
    }
}

void HXOptionTrade::OnRspQryTrade(TORASPAPI::CTORATstpSPTradeField *pTrade, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {

}

void HXOptionTrade::OnRspQryShareholderAccount(TORASPAPI::CTORATstpSPShareholderAccountField* pShareholderAccount, TORASPAPI::CTORATstpSPRspInfoField* pRspInfo, int nRequestID, bool bIsLast)
{
    INIT_PROMISE(String);
    if (pRspInfo->ErrorID != 0) {
        FATAL("get shareholder account fail: {} {}", pRspInfo->ErrorID, to_utf8(pRspInfo->ErrorMsg));
        return;
    }
    String acc(pShareholderAccount->ShareholderID, strlen(pShareholderAccount->ShareholderID));
    SET_PROMISE(acc);
}
