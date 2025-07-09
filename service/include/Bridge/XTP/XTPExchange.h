#pragma once
#include "../exchange.h"
#include "XTPQuote.h"
#include "XTPTrade.h"
#include <map>
#include <mutex>
#include <condition_variable>

class XTPExchange : public ExchangeInterface {
public:
  XTPExchange(Server* server);

  ~XTPExchange();

  virtual const char* Name() { return "XTP"; }
  virtual bool Init(const ExchangeInfo& handle);

  virtual bool Release();

  virtual bool Login();
  virtual bool IsLogin();

  virtual void SetFilter(const QuoteFilter& filter);

  AccountPosition GetPosition();

  AccountAsset GetAsset();

  bool AddOrder(const String& symbol, Order& order);

  bool UpdateOrder(order_id id);

  bool CancelOrder(order_id id);
  
  OrderList GetOrders();

  Order GetOrder(const order_id& id);

  QuoteInfo GetQuote(const String& symbol);

  std::mutex& GetMutex(int request_id) { return _mtxs[request_id]; }
  std::condition_variable& GetCV(int request_id) { return _cvs[request_id]; }

  void QueryQuotes();

  void StopQuery();
  
private:
  bool _requested: 1;
  bool _login_status: 1;

  XTP::API::QuoteApi* m_pQuoteApi;
  XTP::API::TraderApi* m_pTradeApi;
  XTPQuote* m_pQuote;
  XTPTrade* m_pTrade;

  uint64_t m_session;

  ExchangeInfo _handle;

  std::map<int, std::condition_variable> _cvs;
  std::map<int, std::mutex> _mtxs;

  std::map<uint64_t, XTPOrderInsertInfo*> _orders;

  QuoteFilter _filter;

  List<Pair<float, float>> _workingTime;
};