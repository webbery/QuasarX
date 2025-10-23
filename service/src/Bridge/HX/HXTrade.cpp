#include "Bridge/HX/HXTrade.h"
#include "Bridge/exchange.h"
#include "Bridge/HX/HXExchange.h"

HXTrade::HXTrade(HXExchange* exc):_exchange(exc) {

}

void HXTrade::OnRspError(TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast) {

}
void HXTrade::OnRspOrderInsert(TORASTOCKAPI::CTORATstpInputOrderField *pInputOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {
    
}
void HXTrade::OnRtnOrder(TORASTOCKAPI::CTORATstpOrderField *pOrderField) {

}

void HXTrade::OnErrRtnOrderInsert(TORASTOCKAPI::CTORATstpInputOrderField *pInputOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID) {

}

void HXTrade::OnRtnTrade(TORASTOCKAPI::CTORATstpTradeField *pTradeField) {
    order_id id{0};
    id._id = atol(pTradeField->OrderLocalID);
    TradeReport report;
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
