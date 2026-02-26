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

  virtual bool Login(AccountType t);
  virtual bool IsLogin();
  virtual void Logout(AccountType t);

  virtual bool GetSymbolExchanges(List<Pair<String, ExchangeName>>& info);
  virtual void SetFilter(const QuoteFilter& filter);
  void UseLevel(int level);

  virtual bool GetPosition(AccountPosition&);

  virtual AccountAsset GetAsset();
  
  order_id AddOrder(const symbol_t& symbol, OrderContext* order);

  virtual void OnOrderReport(order_id id, const TradeReport& report);

  virtual Boolean CancelOrder(order_id id, OrderContext* order);

  virtual bool GetOrders(SecurityType type, OrderList& ol);
  virtual bool GetOrder(const String& sysID, Order& ol);

  virtual void QueryQuotes();

  virtual void StopQuery() {}

  virtual QuoteInfo GetQuote(symbol_t);

  virtual double GetAvailableFunds();
  virtual bool GetCommission(symbol_t symbol, List<Commission>& comms);
  virtual Boolean HasPermission(symbol_t symbol);
  virtual void Reset();
  virtual void GetFee(FeeInfo& fee, symbol_t symbol) {}

  double Progress();
  void SetCommission(const Commission& buy, const Commission& sell);
  void SetSlippage(float slippage) { _slippage = slippage; }

  virtual int GetStockLimitation(char type);

  virtual bool SetStockLimitation(char type, int limitation);
private:
  bool Once(symbol_t symbol, time_t timeAxis);
  bool Once(uint32_t& curIndex);

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

  Map<symbol_t, DataFrame> _csvs;
  Map<symbol_t, Vector<String>> _headers;
  Map<symbol_t, QuoteInfo> _quotes;

  uint32_t _cur_index;
  std::thread* _worker;

  std::mutex _mx;
  std::condition_variable _cv;

  ConcurrentMap<symbol_t, boost::lockfree::queue<OrderInfo>*> _orders;
  std::atomic<size_t> _cur_id;
  ConcurrentMap<size_t, OrderContext*> _reports;
  Commission _buy;
  Commission _sell;
  float _slippage;  // 滑点
};