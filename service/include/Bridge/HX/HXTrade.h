#pragma once
#include "DataGroup.h"
#include "hx/TORATstpTraderApi.h"

class HXExchange;
class HXTrade: public TORASTOCKAPI::CTORATstpTraderSpi {
public:
    HXTrade(HXExchange* );

    OrderList& GetOrders() { return _orders; }

    ///登录响应
    virtual void OnRspUserLogin(TORASTOCKAPI::CTORATstpRspUserLoginField* pRspUserLoginField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID);

    virtual void OnRspError(TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast);
    ///报单录入响应
    virtual void OnRspOrderInsert(TORASTOCKAPI::CTORATstpInputOrderField *pInputOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID);
    
    virtual void OnRtnOrder(TORASTOCKAPI::CTORATstpOrderField *pOrderField);
    ///报单错误回报
    virtual void OnErrRtnOrderInsert(TORASTOCKAPI::CTORATstpInputOrderField *pInputOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID);
    
    ///成交回报
    virtual void OnRtnTrade(TORASTOCKAPI::CTORATstpTradeField *pTradeField);
    
    ///撤单响应
    virtual void OnRspOrderAction(TORASTOCKAPI::CTORATstpInputOrderActionField *pInputOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID);
    
    ///撤单错误回报
    virtual void OnErrRtnOrderAction(TORASTOCKAPI::CTORATstpInputOrderActionField *pInputOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID);
    
    ///条件单录入响应
    virtual void OnRspCondOrderInsert(TORASTOCKAPI::CTORATstpInputCondOrderField *pInputCondOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID);
    
    ///条件单回报
    virtual void OnRtnCondOrder(TORASTOCKAPI::CTORATstpConditionOrderField *pConditionOrderField);
    
    ///条件单录入错误回报
    virtual void OnErrRtnCondOrderInsert(TORASTOCKAPI::CTORATstpInputCondOrderField *pInputCondOrderField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID);
    
    ///条件单撤单响应
    virtual void OnRspCondOrderAction(TORASTOCKAPI::CTORATstpInputCondOrderActionField *pInputCondOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID);
    
    ///条件单撤单错误回报
    virtual void OnErrRtnCondOrderAction(TORASTOCKAPI::CTORATstpInputCondOrderActionField *pInputCondOrderActionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID);
    
    ///查询资金账户响应
    virtual void OnRspQryTradingAccount(TORASTOCKAPI::CTORATstpTradingAccountField* pTradingAccountField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID, bool bIsLast);
    ///查询报单响应
    virtual void OnRspQryOrder(TORASTOCKAPI::CTORATstpOrderField* pOrderField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID, bool bIsLast);

private:
    HXExchange* _exchange;

    OrderList _orders;
};
