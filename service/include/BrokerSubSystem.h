#pragma once
#include "std_header.h"
#include "Bridge/exchange.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Util/lmdb.h"
#include "DataHandler.h"
#include "json.hpp"
#include "server.h"
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <condition_variable>
#include "PortfolioSubsystem.h"

class Broker {
public:
  virtual ~Broker() {}

  virtual int Buy(symbol_t, const Order& order, DealDetail& detail) = 0;

  virtual int Sell(symbol_t, const Order& order, DealDetail& detail) = 0;

  virtual double Put(symbol_t, const Order& order)  {return 0; }

  virtual double Call(symbol_t, const Order& order) {return 0; }

  virtual uint32_t Statistic(float confidence, int N, std::shared_ptr<DataGroup> group, nlohmann::json& indexes) = 0;

  virtual const Asset& GetAsset(const String& symbol) = 0;
};

class BrokerSubSystem: public Broker {
public:
  using predictions_t = List<Pair<fixed_time_range, int>>;

  BrokerSubSystem(Server* server, ExchangeHandler* handler):_thread(nullptr), _exit(false) {
    _portfolio = server->GetPortforlioSubSystem();
    _handler = handler;
    if (!_handler) {
      _exchange = server->GetExchange(ExchangeType::EX_SIM);
    }
  }

  virtual ~BrokerSubSystem() { Release(); }

  bool Init(const char* dbpath, double capital = 0);

  void Release();

  int Buy(symbol_t symbol, const Order& order, DealDetail& detail);

  int Sell(symbol_t symbol, const Order& order, DealDetail& detail);

  // 统计当前指标
  uint32_t Statistic(float confidence, int N, std::shared_ptr<DataGroup> group, nlohmann::json& indexes);

  double GetProfitLoss();
  
  const Asset& GetAsset(const String& symbol);

  void SetStockCommission(const Commission& comm);
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
  int BuyStock(symbol_t symbol, const Order& order, DealInfo& deal);
  int SellStock(symbol_t symbol, const Order& order, DealInfo& deal);

  double SimulateMatchStockBuyer(symbol_t symbol,double capital, const Order& order, DealInfo& deal);
  double SimulateMatchStockSeller(symbol_t symbol, const Order& order, DealInfo& deal);
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

private:
  PortfolioSubSystem* _portfolio;
  ExchangeHandler* _handler;
  ExchangeInterface* _exchange;
  struct LockTransactions {
    List<Transaction> _transactions;
    std::mutex _mtx;
  };

  Map<symbol_t, LockTransactions> _trans;

  std::shared_mutex _predMtx;
  Map<symbol_t, predictions_t> _predictions;
  Map<symbol_t, Pair<fixed_time_range, int>> _symbolOperation;
  
  Map<int, MDB_dbi> _dbis;
  // 交易手续费
  Commission _stock;
  Map<symbol_t, Commission> _future;

  bool _update: 1;
  bool _exit: 1;
  float _slip;
  // 本金
  double _principal;
  String _dbpath;
  std::thread* _thread;
  std::mutex _mutex;
  std::condition_variable _cv;
};

// class VirtualBroker: public Broker {
// public:
//   VirtualBroker(Server* server, double capital): _server(server){ _portfolio._principal = capital; }

//   virtual double Buy(const String& symbol, const Order& order);

//   virtual double Sell(const String& symbol, const Order& order);
//   virtual uint32_t Statistic(float confidence, int N, std::shared_ptr<Da😅☺️☺️☺️☺️taGroup> group, nlohmann::json& indexes);

//   const Asset& GetAsset(const String& symbol);
// private:
//   List<Pair<String, Order>> _orders;
//   PortfolioInfo _portfolio;
//   Server* _server;
// };