#pragma once
#include "hx/TORATstpSPTraderApi.h"

class HXExchange;
class HXOptionTrade : public TORASPAPI::CTORATstpSPTraderSpi{
public:
    HXOptionTrade(HXExchange* exchange);

    void Release();

    virtual void OnFrontConnected();
    virtual void OnFrontDisconnected(int nReason);

    virtual void OnRspError(TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast);
	
    virtual void OnRspOrderInsert(TORASPAPI::CTORATstpSPInputOrderField *pInputOrderField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID);
	
    virtual void OnRtnOrder(TORASPAPI::CTORATstpSPOrderField *pOrder);

    virtual void OnErrRtnOrderInsert(TORASPAPI::CTORATstpSPInputOrderField *pInputOrder, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo,int nRequestID) ;

    virtual void OnRspOrderAction(TORASPAPI::CTORATstpSPInputOrderActionField *pInputOrderActionField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID);
	
    virtual void OnRtnTrade(TORASPAPI::CTORATstpSPTradeField *pTrade);

    virtual void OnRspExerciseInsert(TORASPAPI::CTORATstpSPInputExerciseField *pInputExerciseField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID) ;
	
    virtual void OnRtnExercise(TORASPAPI::CTORATstpSPExerciseField *pExercise);

    virtual void OnRspExerciseAction(TORASPAPI::CTORATstpSPInputExerciseActionField *pInputExerciseActionField, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID) ;
    //报单查询
    virtual void OnRspQryOrder(TORASPAPI::CTORATstpSPOrderField *pOrder, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast); 
    
    //成交查询
    virtual void OnRspQryTrade(TORASPAPI::CTORATstpSPTradeField *pTrade, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast); 
    //查询基础交易费用
    virtual void OnRspQryTradingFee(TORASPAPI::CTORATstpSPTradingFeeField *pTradingFee, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {}; 
    
    //查询佣金费率
    virtual void OnRspQryInvestorTradingFee(TORASPAPI::CTORATstpSPInvestorTradingFeeField *pInvestorTradingFee, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {}; 
    
    //查询保证金费率
    virtual void OnRspQryInvestorMarginFee(TORASPAPI::CTORATstpSPInvestorMarginFeeField *pInvestorMarginFee, TORASPAPI::CTORATstpSPRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {}; 


private:
    HXExchange* _exchange;
};
