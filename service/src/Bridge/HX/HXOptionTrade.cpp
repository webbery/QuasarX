#include "Bridge/HX/HXOptionTrade.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"

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

void HXOptionTrade::OnRspError(TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    INFO("OnRspError {}.", to_utf8(pRspInfo->ErrorMsg));
}

void HXOptionTrade::OnRspOrderInsert(TORASPAPI::CTORATstpSPInputOrderField *pInputOrderField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID) {

}

void HXOptionTrade::OnRtnOrder(TORASPAPI::CTORATstpSPOrderField *pOrder) {

}

void HXOptionTrade::OnErrRtnOrderInsert(TORASPAPI::CTORATstpSPInputOrderField *pInputOrder, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo,int nRequestID) {

}

void HXOptionTrade::OnRspOrderAction(TORASPAPI::CTORATstpSPInputOrderActionField *pInputOrderActionField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID) {

}

void HXOptionTrade::OnRtnTrade(TORASPAPI::CTORATstpSPTradeField *pTrade) {

}

void HXOptionTrade::OnRspExerciseInsert(TORASPAPI::CTORATstpSPInputExerciseField *pInputExerciseField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID) {

}

void HXOptionTrade::OnRtnExercise(TORASPAPI::CTORATstpSPExerciseField *pExercise) {

}

void HXOptionTrade::OnRspExerciseAction(TORASPAPI::CTORATstpSPInputExerciseActionField *pInputExerciseActionField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID) {

}

void HXOptionTrade::OnRspQryOrder(TORASPAPI::CTORATstpSPOrderField *pOrder, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {

}

void HXOptionTrade::OnRspQryTrade(TORASPAPI::CTORATstpSPTradeField *pTrade, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {

}
