#pragma once
#include "Bridge/exchange.h"
#include "DataFrame/DataFrame.h"
#include "Util/system.h"
#include <nng/nng.h>
#include <boost/lockfree/queue.hpp>

using DataFrame = hmdf::StdDataFrame<uint32_t>;
class Server;

// 历史数据仿真,使用历史数据并在中间进行插值模拟
class StockHistorySimulation : public ExchangeInterface {
public:
  StockHistorySimulation(Server*);
  ~StockHistorySimulation();

  virtual const char* Name() { return STOCK_HISTORY_SIM; }
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

  // 获取原始价格（未复权），用于回测时实际买卖
  double GetPrimitivePrice(symbol_t symbol, uint32_t index);
  // 获取复权价格，用于指标计算
  double GetAdjPrice(symbol_t symbol, uint32_t index);

  // 获取指定 symbol 的持仓数量
  int64_t GetPositionQuantity(symbol_t symbol) const;

  // 合约信息查询接口
  virtual bool GetAllStockSymbols(List<SymbolInfo>& symbols) override;
  virtual bool GetAllFundSymbols(List<SymbolInfo>& symbols) override;
  virtual bool GetAllOptionSymbols(List<SymbolInfo>& symbols) override;
  virtual SymbolInfo GetSymbolInfo(const String& code) override;
  virtual void RefreshSymbolList() override;
private:
  bool Once(symbol_t symbol, uint32_t& curIndex);
  // 同一时刻只能有一个线程调用
  bool Once(uint32_t& curIndex);

  void Worker();
  bool LoadCSVToDataFrame(const String& file_path, DataFrame& df, Vector<String>& header);
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

  Map<symbol_t, DataFrame> _csvs;       // 复权数据（用于指标计算）
  Map<symbol_t, DataFrame> _org_csvs;  // 原始数据（用于实际买卖）
  Map<symbol_t, Vector<String>> _headers;
  Map<symbol_t, Vector<String>> _org_headers;
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

  // 持仓跟踪：symbol -> 持仓数量
  Map<symbol_t, int64_t> _positions;
  mutable std::mutex _positionMtx;  // 保护持仓数据
};