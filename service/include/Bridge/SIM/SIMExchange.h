#pragma once
#include "Bridge/exchange.h"
#include "DataFrame/DataFrame.h"
#include "Util/system.h"
#include <nng/nng.h>
#include <boost/lockfree/queue.hpp>

using DataFrame = hmdf::StdDataFrame<uint32_t>;
class Server;

// 仿真,使用历史数据并在中间进行插值模拟
class StockSimulation : public ExchangeInterface {
public:
  StockSimulation(Server*);
  ~StockSimulation();

  virtual const char* Name() { return "SIM"; }
  virtual bool Init(const ExchangeInfo& handle);
  virtual bool Release();

  virtual bool Login();
  virtual bool IsLogin();

  virtual bool GetSymbolExchanges(List<Pair<String, ExchangeName>>& info);
  virtual void SetFilter(const QuoteFilter& filter);
  void UseLevel(int level);

  virtual AccountPosition GetPosition();

  virtual AccountAsset GetAsset();
  
  order_id AddOrder(const symbol_t& symbol, OrderContext* order);

  virtual void OnOrderReport(order_id id, const TradeReport& report);

  virtual bool CancelOrder(order_id id);

  virtual bool GetOrders(OrderList& ol);

  virtual void QueryQuotes();

  virtual void StopQuery() {}

  virtual QuoteInfo GetQuote(symbol_t) { return QuoteInfo();}

  virtual double GetAvailableFunds();
private:
  void Worker();
  void LoadT0(const String& code);
  void LoadT1(const String& code);
  // TODO: 订单撮合
  TradeReport OrderMatch(const Order& order, const QuoteInfo& quote);

  struct OrderInfo {
      size_t _id;
      OrderContext* _order;
  };

protected:
  String _org_path;
  nng_socket _sock;
  bool _finish;

  Map<String, DataFrame> _csvs;
  Map<String, Vector<String>> _headers;
  Map<String, QuoteInfo> _rows;

  int _cur_index;
  std::thread* _worker;

  std::mutex _mx;
  std::condition_variable _cv;

  ConcurrentMap<symbol_t, boost::lockfree::queue<OrderInfo>*> _orders;
  size_t _cur_id;
  ConcurrentMap<size_t, OrderContext*> _reports;
};