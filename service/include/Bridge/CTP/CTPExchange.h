#pragma once
#include "Util/system.h"
#include "ctp/ThostFtdcTraderApi.h"
#include "ctp/ThostFtdcMdApi.h"
#include "Bridge/exchange.h"
#include "Bridge/CTP/CTPQuote.h"
#include "Bridge/CTP/CTPTrade.h"

class CTPExchange : public ExchangeInterface {
public:
    CTPExchange(Server* server);
    ~CTPExchange();

    virtual const char* Name() { return "CTP"; }
    virtual bool Init(const ExchangeInfo& handle);

    virtual bool Release();

    virtual bool Login();
    virtual bool IsLogin();

  virtual void SetFilter(const QuoteFilter& filter);

    virtual AccountPosition GetPosition();

    virtual AccountAsset GetAsset();
    
    virtual bool AddOrder(const String& symbol, Order& order);

    virtual bool UpdateOrder(order_id id);

    virtual bool CancelOrder(order_id id);

    virtual OrderList GetOrders();

    virtual void QueryQuotes();

    virtual void StopQuery() {}

    QuoteInfo GetQuote(symbol_t symbol);

    const Commission& GetCommission(const symbol_t& symbol);
    /**
     * @brief 更新手续费
     * 
     */
    void UpdateCommission();

private:
    CThostFtdcMdApi* _pUserMdApi;
    CThostFtdcTraderApi* _pUserTradeApi;

    CTPQuote* _quote;
    CTPTrade* _trade;

    int _nRequestID;
    ExchangeInfo _info;

    List<StringView> _contracts;

    Map<symbol_t, Commission> _commissions;
};
