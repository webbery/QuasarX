#pragma once
#include "Bridge/exchange.h"
#include "DataFrame/DataFrame.h"
#include <nng/nng.h>

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

  virtual void SetFilter(const QuoteFilter& filter);

  virtual AccountPosition GetPosition();

  virtual AccountAsset GetAsset();
  
  virtual bool AddOrder(const String& symbol, Order& order);

  virtual bool UpdateOrder(order_id id);

  virtual bool CancelOrder(order_id id);

  virtual OrderList GetOrders();

  virtual void QueryQuotes();

  virtual void StopQuery() {}

  virtual QuoteInfo GetQuote(const String&) { return QuoteInfo();}
protected:
  String _org_path;
  nng_socket _sock;

  Map<String, DataFrame> _csvs;
  List<String> _headers;
  Map<String, QuoteInfo> _rows;

  QuoteFilter _filter;
};