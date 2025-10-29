#pragma once
#include "../exchange.h"
#include "XTPQuote.h"
#include "XTPTrade.h"
#include <map>
#include <mutex>
#include <condition_variable>

class XTPExchange : public ExchangeInterface {
  friend class XTPQuote;
  friend class XTPTrade;
public:
  XTPExchange(Server* server);

  ~XTPExchange();

  virtual const char* Name() { return "XTP"; }
  virtual bool Init(const ExchangeInfo& handle);

  virtual bool Release();

  virtual bool Login();
  virtual bool IsLogin();

  virtual bool GetSymbolExchanges(List<Pair<String, ExchangeName>>& info);
  virtual void SetFilter(const QuoteFilter& filter);

  virtual bool GetPosition(AccountPosition&);

  AccountAsset GetAsset();

  order_id AddOrder(const symbol_t& symbol, OrderContext* order);

  void OnOrderReport(order_id id, const TradeReport& report);

  bool CancelOrder(order_id id);
  
  virtual bool GetOrders(OrderList& ol);

  Order GetOrder(const order_id& id);

  QuoteInfo GetQuote(symbol_t symbol);

  std::mutex& GetMutex(int request_id) { return _mtxs[request_id]; }
  std::condition_variable& GetCV(int request_id) { return _cvs[request_id]; }

  void QueryQuotes();

  void StopQuery();
  
  virtual double GetAvailableFunds();
    virtual Commission GetCommission(symbol_t symbol);
private:
  bool _requested: 1;
  bool _login_status: 1;
  bool _quote_inited: 1;

  XTP::API::QuoteApi* m_pQuoteApi;
  XTP::API::TraderApi* m_pTradeApi;
  XTPQuote* m_pQuote;
  XTPTrade* m_pTrade;

  uint64_t m_session;

  ExchangeInfo _handle;

  std::map<int, std::condition_variable> _cvs;
  std::map<int, std::mutex> _mtxs;

  using concurrent_order_map = ConcurrentMap<uint64_t, Pair<XTPOrderInsertInfo*, OrderContext*>>;
  concurrent_order_map _orders;

  List<Pair<float, float>> _workingTime;
};