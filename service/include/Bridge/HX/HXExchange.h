#pragma once
#include "Bridge/exchange.h"

class HXQuateSpi;
namespace TORALEV1API {
    class CTORATstpXMdApi;
}
class HXExchange: public ExchangeInterface {
public:
    HXExchange(Server* server);
    ~HXExchange();

    virtual const char* Name();

    virtual bool Init(const ExchangeInfo& handle);

    virtual void SetFilter(const QuoteFilter& filter);

    virtual bool Release();

    virtual bool Login();
    virtual bool IsLogin();

    virtual AccountPosition GetPosition();

    virtual AccountAsset GetAsset();
    
    virtual order_id AddOrder(const symbol_t& symbol, OrderContext* order);

    virtual void OnOrderReport(order_id id, const TradeReport& report);

    virtual bool CancelOrder(order_id id);
    // 获取当前尚未完成的所有订单
    virtual OrderList GetOrders();

    virtual void QueryQuotes();

    virtual void StopQuery();

    virtual QuoteInfo GetQuote(symbol_t symbol);

private:
    HXQuateSpi* _quote;
    TORALEV1API::CTORATstpXMdApi* _quoteAPI;
};
