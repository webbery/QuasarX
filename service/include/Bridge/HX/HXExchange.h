#pragma once
#include "Bridge/exchange.h"
#include <cstdint>
#include <memory>

#define INIT_PROMISE(type) \
auto itr = _exchange->_promises.find(nRequestID);\
if (itr == _exchange->_promises.end())\
    return;\
PromisePtr<type> prom = static_pointer_cast<std::promise<type>>(_exchange->_promises[nRequestID])

#define SET_PROMISE(value) prom->set_value(value); _exchange->_promises.erase(nRequestID)

template<typename T>
using PromisePtr = std::shared_ptr<std::promise<T>>;

class OrderLimit;
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
    HXTrade* _trade;
    TORASTOCKAPI::CTORATstpTraderApi* _tradeAPI;
    String _shareholder[MT_COUNT];    // 股东账号

    unsigned short _currentCount;   // 当前交易(下单)次数
    unsigned short _dailyLimit = 1000; // 日内交易最大次数
    unsigned short _insertLimitation = 20;
    unsigned short _cancelLimitation = 20;
    OrderLimit* _insertLimit;   // 报单限流
    OrderLimit* _cancelLimit;   // 撤单限流
    int8_t _maxTradeReq;    // 交易通道每秒最大请求数
    int8_t _maxQuoteReq;    // 查询通道每秒最大请求数

    Set<char> _privileges;  // 没有交易权限的板块
};

struct OptionHandle {
    HXOptionTrade* _trade;
    TORASPAPI::CTORATstpSPTraderApi* _tradeAPI;
    String _shareholder[MT_COUNT];    // 股东账号
    unsigned short _dailyLimit; // 日内交易最大次数
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

    virtual Boolean CancelOrder(order_id id, OrderContext* order);
    // 获取当前尚未完成的所有订单
    virtual bool GetOrders(SecurityType type, OrderList& ol);
    virtual bool GetOrder(const String& sysID, Order& ol);

    virtual void QueryQuotes();

    virtual void StopQuery();

    virtual QuoteInfo GetQuote(symbol_t symbol);

    virtual double GetAvailableFunds();

    virtual bool GetCommission(symbol_t symbol, List<Commission>& comms);
    virtual Boolean HasPermission(symbol_t symbol);
    virtual void Reset();

    virtual int GetStockLimitation(char type);

    virtual bool SetStockLimitation(char type, int limitation);

    virtual void GetFee(FeeInfo& fee, symbol_t symbol);

    bool QuoteLogin();
    // 重新订阅行情
    bool ResubscribeQuote();
private:
    // 查询股东用户
    bool QueryStockShareHolder(ExchangeName name);
    bool QueryOptionShareHolder(ExchangeName name);

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

    bool StockLogin();
    bool OptionLogin();
    
    order_id AddStockOrder(const symbol_t& symbol, OrderContext* order);
    order_id AddOptionOrder(const symbol_t& symbol, OrderContext* order);

    void CancelStockOrder(order_id id, OrderContext* order);
    void CancelOptionOrder(order_id id, OrderContext* order);

    void SubscribeStockQuote(const Map<char, Vector<String>>& stocks);
    void SubscribeOptionQuote(const Map<char, Vector<String>>& options);
    void UnSubscribeStockQuote(const Map<char, Vector<String>>& stocks);

    bool QueryStockOrders(uint64_t reqID);
    bool QueryOptionOrders(uint64_t reqID);

    void GenerateSubscribeStocks(Map<char, Vector<String>>& subs);

    Boolean HasStockPermission(symbol_t symbol);

    bool InitPermissions(ExchangeName name);
    // 重连行情
    bool ReconnectQuote();
    // 重连交易通道
    bool ReconnectTrade();
private:
    bool _login_status : 1;
    bool _quote_inited : 1;
    bool _requested : 1;
    bool _stock_login : 1;
    bool _quote_login : 1;
    bool _option_login : 1;

    ExchangeInfo _brokerInfo;

    char _current;      // 当前帐号索引

    HXQuateSpi* _quote;
    TORALEV1API::CTORATstpXMdApi* _quoteAPI;

    OptionHandle _optionHandle;
    StockHandle _stockHandle;

    

    std::atomic<uint32_t> _reqID;
    using concurrent_order_map = ConcurrentMap<uint64_t, OrderContext*>;
    concurrent_order_map _orders;

    std::mutex _mutex;
    std::unordered_map<uint32_t, std::shared_ptr<void>> _promises;

    Map<symbol_t, Set<Commission*>> _commissions;
};
