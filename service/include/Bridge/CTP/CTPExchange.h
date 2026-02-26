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

    virtual bool Login(AccountType t);
    virtual bool IsLogin();
    virtual void Logout(AccountType t);

    virtual bool GetSymbolExchanges(List<Pair<String, ExchangeName>>& info);
    virtual void SetFilter(const QuoteFilter& filter);

    virtual bool GetPosition(AccountPosition&);

    virtual AccountAsset GetAsset();
    
    virtual order_id AddOrder(const symbol_t& symbol, OrderContext* order);

    virtual void OnOrderReport(order_id id, const TradeReport& report);

    virtual Boolean CancelOrder(order_id id, OrderContext* order);

    virtual bool GetOrders(SecurityType type, OrderList& ol);
    virtual bool GetOrder(const String& sysID, Order& ol);

    virtual void QueryQuotes();

    virtual void StopQuery() {}

    virtual void GetFee(FeeInfo& fee, symbol_t symbol) {}
    
    QuoteInfo GetQuote(symbol_t symbol);

    virtual bool GetCommission(symbol_t symbol, List<Commission>& comms);
    /**
     * @brief 更新手续费
     * 
     */
    void UpdateCommission();

    virtual double GetAvailableFunds();
    virtual Boolean HasPermission(symbol_t symbol);
    virtual void Reset();

    virtual int GetStockLimitation(char type);

    virtual bool SetStockLimitation(char type, int limitation);
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
