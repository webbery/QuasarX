#pragma once
#include "Bridge/exchange.h"

class HXQuateSpi;
class HXTrade;
namespace TORALEV1API {
    class CTORATstpXMdApi;
}
namespace TORASTOCKAPI {
    class CTORATstpTraderApi;
    struct CTORATstpInputOrderField;
}
class HXExchange: public ExchangeInterface {
    friend class HXQuateSpi;
    friend class HXTrade;
public:
    HXExchange(Server* server);
    ~HXExchange();

    virtual const char* Name();

    virtual bool Init(const ExchangeInfo& handle);

    virtual void SetFilter(const QuoteFilter& filter);

    virtual bool Release();

    virtual bool Login();
    virtual bool IsLogin();

    virtual bool GetSymbolExchanges(List<Pair<String, ExchangeName>>& info);
    virtual AccountPosition GetPosition();

    virtual AccountAsset GetAsset();
    
    virtual order_id AddOrder(const symbol_t& symbol, OrderContext* order);

    virtual void OnOrderReport(order_id id, const TradeReport& report);

    virtual bool CancelOrder(order_id id);
    // 获取当前尚未完成的所有订单
    virtual bool GetOrders(OrderList& ol);

    virtual void QueryQuotes();

    virtual void StopQuery();

    virtual QuoteInfo GetQuote(symbol_t symbol);

    virtual double GetAvailableFunds();

private:
    // 查询股东用户
    bool QueryShareHolder();

private:
    bool _login_status : 1;
    bool _quote_inited : 1;
    bool _requested : 1;
    bool _trader_login : 1;
    bool _quote_login : 1;

    uint32_t _port;

    String _user;
    String _pwd;

    String _account;    // 投资者账号
    String _accpwd;

    String _shareholder;    // 股东账号

    HXQuateSpi* _quote;
    HXTrade* _trade;
    TORALEV1API::CTORATstpXMdApi* _quoteAPI;
    TORASTOCKAPI::CTORATstpTraderApi* _tradeAPI;

    int8_t _maxInsertOrder; // 每秒最大报单笔数
    int8_t _maxCancelOrder; // 每秒最大撤单笔数
    int8_t _maxTradeReq;    // 交易通道每秒最大请求数
    int8_t _maxQuoteReq;    // 查询通道每秒最大请求数

    std::atomic<uint32_t> _reqID;
    using concurrent_order_map = ConcurrentMap<uint64_t, Pair<TORASTOCKAPI::CTORATstpInputOrderField*, OrderContext*>>;
    concurrent_order_map _orders;

    std::mutex _mutex;
    std::unordered_map<uint32_t, std::shared_ptr<void>> _promises;
};
