#pragma once
#include "Bridge/exchange.h"
#include "DataFrame/DataFrame.h"
#include "Util/system.h"
#include "Bridge/SIM/BacktestContext.h"
#include <nng/nng.h>
#include <atomic>
#include <shared_mutex>
#include <memory>

using DataFrame = hmdf::StdDataFrame<uint32_t>;
class Server;

// 历史数据仿真，使用历史数据并在中间进行插值模拟
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

  void SetCommission(const Commission& buy, const Commission& sell);
  void SetSlippage(float slippage) { _slippage = slippage; }

  virtual int GetStockLimitation(char type);

  virtual bool SetStockLimitation(char type, int limitation);

  // ============ 多线程回测支持接口 ============

  /**
   * @brief 创建新的回测上下文
   * @param strategy_name 策略名称
   * @param symbols 该策略涉及的标的列表
   * @param initial_capital 初始资金
   * @return 回测上下文 ID（用于后续获取）
   */
  uint16_t createBacktestContext(
      const String& strategy_name,
      const Set<symbol_t>& symbols,
      double initial_capital = 100000.0
  );

  /**
   * @brief 获取指定回测上下文
   * @param run_id 回测运行 ID
   * @return 回测上下文指针，不存在返回 nullptr
   */
  BacktestContext* getBacktestContext(uint16_t run_id);

  /**
   * @brief 获取指定回测上下文（const 版本）
   */
  const BacktestContext* getBacktestContext(uint16_t run_id) const;

  /**
   * @brief 销毁回测上下文
   */
  void destroyBacktestContext(uint16_t run_id);

  /**
   * @brief 推进指定回测上下文的时间
   * @param context 回测上下文
   * @return 是否还有下一个时间点
   */
  bool stepForward(BacktestContext* context);

  /**
   * @brief 获取指定回测上下文的报价
   * @param symbol 标的
   * @param strategy 策略名称（用于查找对应的回测上下文）
   */
  QuoteInfo GetQuote(symbol_t symbol, const String& strategy);

  /**
   * @brief 线程安全的订单提交（多线程回测模式）
   * @param symbol 标的
   * @param order 订单上下文
   * @param strategy 策略名称（用于查找对应的回测上下文）
   */
  order_id AddOrder(const symbol_t& symbol, OrderContext* order, uint32_t strategy_hash);

  /**
   * @brief 获取原始价格（未复权），用于回测时实际买卖
   */
  double GetPrimitivePrice(symbol_t symbol, uint32_t index) const;

  /**
   * @brief 获取复权价格，用于指标计算
   */
  double GetAdjPrice(symbol_t symbol, uint32_t index) const;

  // 获取指定 symbol 的持仓数量
  int64_t GetPositionQuantity(symbol_t symbol) const;

  void InitializeCapital(double capital) {
      _capital = capital;
      _availableFunds.store(capital, std::memory_order_relaxed);
  }

  // 合约信息查询接口
  virtual bool GetAllStockSymbols(List<SymbolInfo>& symbols) override;
  virtual bool GetAllFundSymbols(List<SymbolInfo>& symbols) override;
  virtual bool GetAllOptionSymbols(List<SymbolInfo>& symbols) override;
  virtual SymbolInfo GetSymbolInfo(const String& code) override;
  virtual void RefreshSymbolList() override;

  // 获取回测进度（基于策略）
  double Progress(const String& strategy);
private:
  // ============ 多线程支持方法 ============
  void matchOrders(BacktestContext* context, symbol_t symbol);
  bool LoadCSVToDataFrame(const String& file_path, DataFrame& df, Vector<String>& header);
  void LoadT0(const String& code);
  void LoadT1(const String& code);
  // TODO: 订单撮合
  TradeReport OrderMatch(const Order& order, const QuoteInfo& quote);

  // 清空所有回测数据（行情、订单、持仓等）
  void Clear();

protected:
  String _org_path;
  nng_socket _sock;
  std::atomic<bool> _finish{false};
  std::atomic<bool> _dataLoadSuccess{false};

  // ============ 只读共享数据（线程安全）============
  mutable std::shared_mutex _dataMutex;
  Map<symbol_t, DataFrame> _csvs;       // 复权数据（用于指标计算）
  Map<symbol_t, DataFrame> _org_csvs;   // 原始数据（用于实际买卖）
  Map<symbol_t, Vector<String>> _headers;
  Map<symbol_t, Vector<String>> _org_headers;

  int _freqType;  // 数据频率（1-day, 0-)

  // ============ 线程隔离数据 ============
  // 回测上下文池
  ConcurrentMap<uint16_t, std::unique_ptr<BacktestContext>> _backtestContexts;
  std::atomic<uint16_t> _nextRunId{1};

  // ============ 全局状态 ============
  std::atomic<size_t> _cur_id;
  ConcurrentMap<size_t, OrderContext*> _reports;
  Commission _buy;
  Commission _sell;
  float _slippage;  // 滑点

  double _capital;  // 总本金
  std::atomic<double> _availableFunds{0.0};  // 可用资金
};
