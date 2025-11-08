#pragma once
#include "Bridge/exchange.h"
#include <memory>

class HXQuateSpi;
class HXTrade;
class HXOptionTrade;
namespace TORALEV1API {
    class CTORATstpXMdApi;
}
namespace TORASTOCKAPI {
    class CTORATstpTraderApi;
    struct CTORATstpInputOrderField;
}

namespace TORASPAPI {
    class CTORATstpSPTraderApi;
}

struct StockHandle {
    HXQuateSpi* _quote;
    HXTrade* _trade;
    TORALEV1API::CTORATstpXMdApi* _quoteAPI;
    TORASTOCKAPI::CTORATstpTraderApi* _tradeAPI;
};

class alignas(8) HXExchange: public ExchangeInterface {
    friend class HXQuateSpi;
    friend class HXTrade;
    friend class HXOptionTrade;
public:
    HXExchange(Server* server);
    ~HXExchange();

    virtual const char* Name();

    virtual bool Init(const ExchangeInfo& handle);

    virtual void SetFilter(const QuoteFilter& filter);

    virtual bool Release();

    virtual bool Login(AccountType t);
    virtual bool IsLogin();
    virtual void Logout(AccountType t);

    virtual bool GetSymbolExchanges(List<Pair<String, ExchangeName>>& info);

    virtual bool GetPosition(AccountPosition&);

    virtual AccountAsset GetAsset();
    
    virtual order_id AddOrder(const symbol_t& symbol, OrderContext* order);

    virtual void OnOrderReport(order_id id, const TradeReport& report);

    virtual bool CancelOrder(order_id id, OrderContext* order);
    // 获取当前尚未完成的所有订单
    virtual bool GetOrders(OrderList& ol);
    virtual bool GetOrder(const String& sysID, Order& ol);

    virtual void QueryQuotes();

    virtual void StopQuery();

    virtual QuoteInfo GetQuote(symbol_t symbol);

    virtual double GetAvailableFunds();

    virtual bool GetCommission(symbol_t symbol, List<Commission>& comms);
private:
    // 查询股东用户
    bool QueryShareHolder(ExchangeName name);

    void addPromise(uint64_t reqID, std::shared_ptr<void> promise);

    template<typename T>
    std::shared_ptr<std::promise<T>> initPromise(uint64_t reqID) {
        auto promise = std::make_shared<std::promise<T>>();
        addPromise(reqID, promise);
        return promise;
    }

    bool InitStockHandle();
    bool InitOptionHandle();

    bool InitQuote();
    bool InitStockTrade();
    bool InitOptionTrade();
    
    order_id AddStockOrder(const symbol_t& symbol, OrderContext* order);
    order_id AddOptionOrder(const symbol_t& symbol, OrderContext* order);

    void SubscribeStockQuote(const Map<char, Vector<String>>& stocks);
    void SubscribeOptionQuote(const Map<char, Vector<String>>& options);
private:
    bool _login_status : 1;
    bool _quote_inited : 1;
    bool _requested : 1;
    bool _trader_login : 1;
    bool _quote_login : 1;

    ExchangeInfo _brokerInfo;

    char _current;      // 当前帐号索引

    String _shareholder[MT_COUNT];    // 股东账号

    HXQuateSpi* _quote;
    HXTrade* _trade;
    TORALEV1API::CTORATstpXMdApi* _quoteAPI;
    TORASTOCKAPI::CTORATstpTraderApi* _tradeAPI;

    HXOptionTrade* _optionTrade;
    TORASPAPI::CTORATstpSPTraderApi* _optionTradeAPI;

    int8_t _maxInsertOrder; // 每秒最大报单笔数
    int8_t _maxCancelOrder; // 每秒最大撤单笔数
    int8_t _maxTradeReq;    // 交易通道每秒最大请求数
    int8_t _maxQuoteReq;    // 查询通道每秒最大请求数

    std::atomic<uint32_t> _reqID;
    using concurrent_order_map = ConcurrentMap<uint64_t, Pair<TORASTOCKAPI::CTORATstpInputOrderField*, OrderContext*>>;
    concurrent_order_map _orders;

    std::mutex _mutex;
    std::unordered_map<uint32_t, std::shared_ptr<void>> _promises;

    Map<symbol_t, Set<Commission*>> _commissions;
};
