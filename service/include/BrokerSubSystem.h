#pragma once
#include "std_header.h"
#include "Bridge/exchange.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Util/lmdb.h"
#include "DataGroup.h"
#include "json.hpp"
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <condition_variable>
#include "PortfolioSubsystem.h"
#include <boost/lockfree/queue.hpp>

// class Broker {
// public:
//   virtual ~Broker() {}

//   virtual int Buy(const String& strategy, symbol_t, const Order& order, TradeInfo& detail) = 0;

//   virtual int Sell(const String& strategy, symbol_t, const Order& order, TradeInfo& detail) = 0;

//   virtual double Put(symbol_t, const Order& order)  {return 0; }

//   virtual double Call(symbol_t, const Order& order) { return 0; }

//   // 行权
//   virtual int Exercise(symbol_t, const Order& order, TradeInfo& info) { return 0; }

//   virtual uint32_t Statistic(float confidence, int N, std::shared_ptr<DataGroup> group, nlohmann::json& indexes) = 0;

//   // 异步下单
//   virtual int64_t AddOrder(symbol_t, const Order& order, std::function<void(const TradeReport&)> cb) = 0;
//   // 查询订单
//   virtual bool QueryOrders(OrderList& ol) = 0;
//   virtual int QueryOrder(uint32_t orderID, Order& order) = 0;
//   virtual void CancelOrder(order_id id) = 0;
// };

enum class StatisticIndicator: char {
  VaR,
  ES,
  MaxDrawDown,
  AnualReturn,
  Sharp,
  Calmar,
  Infomation,
  WinRate,
  NumTrades,
  Extreme,
  TotalReturn,
};

class ICommission {
public:
  virtual double GetCommission(symbol_t symbol, int64_t size) = 0;
};

class StockCommission : public ICommission {
public:
  StockCommission();

  virtual double GetCommission(symbol_t symbol, int64_t size);

public:
  double _min = 5;
  double _fee = 0.00001345;
};

class FutureCommission : public ICommission {
public:
  virtual double GetCommission(symbol_t symbol, int64_t size) { return 0; }
};

class OptionCommission: public ICommission {
public:
  virtual double GetCommission(symbol_t symbol, int64_t size) { return 0; }
};

class IndexCommission: public ICommission {
public:
  virtual double GetCommission(symbol_t symbol, int64_t size) { return 0; }
};

class BrokerSubSystem {
public:
    using predictions_t = List<Pair<fixed_time_range, int>>;

    // 开启模拟则全部使用模拟
    BrokerSubSystem(Server* server, bool is_simulation);

    virtual ~BrokerSubSystem() { Release(); }

    bool Init(const nlohmann::json& config, const Map<ExchangeType, ExchangeInterface*>& brokers, double capital = 0);

    void Release();

    order_id Buy(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& detail);

    order_id Sell(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& detail);

    order_id Buy(const String& strategy, symbol_t symbol, const Order& order, std::function<void (const TradeReport&)> cb);

    order_id Sell(const String& strategy, symbol_t symbol, const Order& order, std::function<void (const TradeReport&)> cb);

    int64_t AddOrder(symbol_t, const Order& order, std::function<void(const TradeReport&)> cb);

    virtual bool QueryOrders(OrderList& ol);
    virtual int QueryOrder(const String& sysID, Order& order);
    virtual void CancelOrder(order_id id, std::function<void (const TradeReport&)> cb);

    // 统计当前指标
    uint32_t Statistic(float confidence, int N, std::shared_ptr<DataGroup> group, nlohmann::json& indexes);
    // 注册统计指标
    void RegistIndicator(const String& strategy, StatisticIndicator indicator);
    void UnRegistIndicator(const String& strategy, StatisticIndicator indicator);
    void CleanAllIndicators(const String& strategy);
    const Set<StatisticIndicator> GetIndicatorsName(const String& strategy) const { return _indicators.at(strategy); }
    float GetIndicator(const String& name, StatisticIndicator indicator);

    double GetProfitLoss();

    void SetCommission(symbol_t symbol, const Commission& comm);
    // 设置滑点
    void SetSlip(float val) { _slip = val; }
    // 
    void PredictWithDays(symbol_t symb, int N, int op);
    bool GetNextPrediction(symbol_t symb, fixed_time_range& tr, int& op);
    void DoneForecast(symbol_t symb, int operation);

    const predictions_t& QueryPredictionOfHistory(symbol_t symb);
    const Map<symbol_t, predictions_t>& QueryPredictionOfHistory() { return _predictions; }

    void DeletePrediction(symbol_t, int index);

    const List<Transaction>& GetHistoryTrades(symbol_t) const;

    Set<symbol_t> GetPoolSymbols(const String& name);

private:
    order_id AddOrderBySide(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& detail, int side);
    order_id AddOrderBySide(const String& strategy, symbol_t symbol, const Order& order, int side, std::function<void (const TradeReport&)> cb);

    // 模拟撮合
    double SimulateMatchStockBuyer(symbol_t symbol, double capital, const Order& order, TradeInfo& deal);
    double SimulateMatchStockSeller(symbol_t symbol, const Order& order, TradeInfo& deal);

    order_id AddOrderAsync(OrderContext* order);

private:
    void run();
    
    void flush(MDB_txn* txn, MDB_dbi dbi);

    double VaR(float confidence);
    double ES(double var);
    double Sharp(const String& name);

    MDB_dbi GetDBI(int portfolid_id, MDB_txn* txn);

    void InitPortfolio(MDB_txn* txn, MDB_dbi);
    void InitBrokers(MDB_txn* txn, MDB_dbi);
    void InitHistory(MDB_txn* txn, MDB_dbi);
    void InitPrediction(MDB_txn* txn, MDB_dbi);

    nlohmann::json GetHistoryJson();
    nlohmann::json GetPortfolioJson();
    nlohmann::json GetBrokers();
    nlohmann::json GetPrediction();

    nlohmann::json LoadJson(const String& name, MDB_txn* txn, MDB_dbi);
    void SaveJson(const String& name, MDB_txn* txn, MDB_dbi, const nlohmann::json& jsn);

    ICommission* GetCommision(symbol_t symbol);

    Transaction Order2Transaction(const OrderContext& context);

    
private:
    Server* _server;
    PortfolioSubSystem* _portfolio;
    bool _simulation;
    // 交易记录
    Map<symbol_t, List<Transaction>> _historyTrades;

    std::mutex _indMtx;
    Map<String, Set<StatisticIndicator>> _indicators;

    std::shared_mutex _predMtx;
    Map<symbol_t, predictions_t> _predictions;
    Map<symbol_t, Pair<fixed_time_range, int>> _symbolOperation;

    Map<int, MDB_dbi> _dbis;
    // 交易手续费
    Commission _stockCommission;
    Map<symbol_t, Commission> _future;
    Map<symbol_t, ICommission*> _commissions;

    bool _update : 1;
    bool _exit : 1;
    float _slip;
    // 本金
    double _principal;
    String _dbpath;
    std::thread* _thread;
    std::mutex _mutex;
    std::condition_variable _cv;

    // 订单队列
    boost::lockfree::queue<OrderContext*> _order_queue;
    // 
    static Map<ExchangeType, ExchangeInterface*> _exchanges;
};