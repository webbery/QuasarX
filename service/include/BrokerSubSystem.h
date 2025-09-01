#pragma once
#include "std_header.h"
#include "Bridge/exchange.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Util/lmdb.h"
#include "DataGroup.h"
#include "json.hpp"
#include "server.h"
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <condition_variable>
#include "PortfolioSubsystem.h"
#include <boost/lockfree/queue.hpp>

class Broker {
public:
  virtual ~Broker() {}

  virtual int Buy(symbol_t, const Order& order, TradeInfo& detail) = 0;

  virtual int Sell(symbol_t, const Order& order, TradeInfo& detail) = 0;

  virtual double Put(symbol_t, const Order& order)  {return 0; }

  virtual double Call(symbol_t, const Order& order) { return 0; }

  // 行权
  virtual int Exercise(symbol_t, const Order& order, TradeInfo& info) { return 0; }

  virtual uint32_t Statistic(float confidence, int N, std::shared_ptr<DataGroup> group, nlohmann::json& indexes) = 0;

  virtual const Asset& GetAsset(const String& symbol) = 0;
  // 异步下单
  virtual int64_t AddOrder(symbol_t, const Order& order, std::function<void(const TradeInfo&)> cb) = 0;
};

enum class StatisticIndicator: char {
  VaR,
  ES,
  MaxDrawDown,
  AnualReturn,
  Sharp,
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

class BrokerSubSystem : public Broker {
public:
    using predictions_t = List<Pair<fixed_time_range, int>>;

    // 开启模拟则全部使用模拟
    BrokerSubSystem(Server* server, bool is_simulation)
        :_thread(nullptr), _exit(false), _order_queue(256), _simulation(is_simulation), _server(server){
        _portfolio = server->GetPortforlioSubSystem();
    }

    virtual ~BrokerSubSystem() { Release(); }

    bool Init(const nlohmann::json& config, const Map<ExchangeType, ExchangeInterface*>& brokers, double capital = 0);

    void Release();

    int Buy(symbol_t symbol, const Order& order, TradeInfo& detail);

    int Sell(symbol_t symbol, const Order& order, TradeInfo& detail);

    int64_t AddOrder(symbol_t, const Order& order, std::function<void(const TradeInfo&)> cb);
    // 统计当前指标
    uint32_t Statistic(float confidence, int N, std::shared_ptr<DataGroup> group, nlohmann::json& indexes);
    // 注册统计指标
    bool RegistIndicator(StatisticIndicator indicator);
    void UnRegistIndicator(StatisticIndicator indicator);

    double GetProfitLoss();

    const Asset& GetAsset(const String& symbol);

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

private:
    int AddOrderBySide(symbol_t symbol, const Order& order, TradeInfo& detail, int side);

    // 模拟撮合
    double SimulateMatchStockBuyer(symbol_t symbol, double capital, const Order& order, TradeInfo& deal);
    double SimulateMatchStockSeller(symbol_t symbol, const Order& order, TradeInfo& deal);

    void AddOrderAsync(OrderContext* order);

private:
    void run();

    void flush(MDB_txn* txn, MDB_dbi dbi);

    double VaR(float confidence);
    double ES(double var);

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
    Map<symbol_t, List<Transaction>> _trans;

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