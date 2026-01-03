#pragma once
#include "Bridge/exchange.h"
#include "Util/system.h"
#include "hx/TORATstpTraderApi.h"

class HXExchange;
class HXTrade: public TORASTOCKAPI::CTORATstpTraderSpi {
public:
    HXTrade(HXExchange* );

    OrderList& GetOrders() { return _orders; }
    List<position_t>& GetPositions() { return _positions; }

    virtual void OnFrontConnected();
    virtual void OnFrontDisconnected(int nReason);
    ///登录响应
    virtual void OnRspUserLogin(TORASTOCKAPI::CTORATstpRspUserLoginField* pRspUserLoginField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID);
    
    ///查询股东账户响应
    virtual void OnRspQryShareholderAccount(TORASTOCKAPI::CTORATstpShareholderAccountField* pShareholderAccountField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID, bool bIsLast);

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

    ///查询投资者持仓响应
    virtual void OnRspQryPosition(TORASTOCKAPI::CTORATstpPositionField *pPositionField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast);
    
    ///查询基础交易费率响应
    virtual void OnRspQryTradingFee(TORASTOCKAPI::CTORATstpTradingFeeField *pTradingFeeField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast); 
    
    ///查询佣金费率响应
    virtual void OnRspQryInvestorTradingFee(TORASTOCKAPI::CTORATstpInvestorTradingFeeField *pInvestorTradingFeeField, TORASTOCKAPI::CTORATstpRspInfoField *pRspInfoField, int nRequestID, bool bIsLast); 

    virtual void OnRspQryShareholderSpecPrivilege(TORASTOCKAPI::CTORATstpShareholderSpecPrivilegeField* pShareholderSpecPrivilegeField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID, bool bIsLast);
    ///查询证券信息响应
    virtual void OnRspQrySecurity(TORASTOCKAPI::CTORATstpSecurityField* pSecurityField, TORASTOCKAPI::CTORATstpRspInfoField* pRspInfoField, int nRequestID, bool bIsLast);

private:
    HXExchange* _exchange;

    String _investor;   // 投资者代码

    OrderList _orders;

    List<position_t> _positions;
};
