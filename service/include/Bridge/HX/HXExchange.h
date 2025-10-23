#pragma once
#include "Bridge/HX/HXTrade.h"
#include "Bridge/exchange.h"

class HXQuateSpi;
namespace TORALEV1API {
    class CTORATstpXMdApi;
}
namespace TORASTOCKAPI {
    class CTORATstpTraderApi;
}
class HXExchange: public ExchangeInterface {
    friend class HXQuateSpi;
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
    bool _login_status : 1;
    bool _quote_inited : 1;
    bool _requested : 1;
    String _user;
    String _pwd;

    HXQuateSpi* _quote;
    HXTrade* _trade;
    TORALEV1API::CTORATstpXMdApi* _quoteAPI;
    TORASTOCKAPI::CTORATstpTraderApi* _tradeAPI;

    uint32_t _reqID = 0;
    using concurrent_order_map = ConcurrentMap<uint64_t, Pair<TORASTOCKAPI::CTORATstpInputOrderField*, OrderContext*>>;
    concurrent_order_map _orders;
};
