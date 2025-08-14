#include "BrokerSubSystem.h"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <yas/serialize.hpp>
#include "Bridge/exchange.h"
#include "PortfolioSubsystem.h"
#include "Util/lmdb.h"
#include "Util/string_algorithm.h"
#include "json.hpp"
#include "Util/system.h"
#include "Risk/RiskMetric.h"
#include "server.h"
#include <boost/math/distributions/normal.hpp>
#include "Handler/ExchangeHandler.h"
#include "Strategy.h"

#define ER(expr) {int rc = 0; CHECK((rc = (expr)) == MDB_SUCCESS, #expr);  }
#define CHECK(test, msg) ((test) ? (void)0 : ((void)fprintf(stderr, \
	"%s:%d: [%d] %s: %s\n", __FILE__, __LINE__, rc, msg, mdb_strerror(rc)), abort()))

// 1G for disk
#define DEFAULT_DISK_CACHE_SIZE 1073741824
#define DEFAULT_MAX_SYMBOLS     16

#define DB_PREDICTION   "predict"

bool BrokerSubSystem::Init(const char* dbpath, double principal) {
  _dbpath = dbpath;
  _principal = principal;
  _thread = new std::thread(&BrokerSubSystem::run, this);
  return true;
}

void BrokerSubSystem::Release() {
  if (!_thread)
    return;

  _exit = true;
  _thread->join();
  delete _thread;
  _thread = nullptr;
}

MDB_dbi BrokerSubSystem::GetDBI(int portfolid_id, MDB_txn* txn) {
  MDB_dbi dbi = -1;
  auto itr = _dbis.find(portfolid_id);
  if (itr != _dbis.end()) {
    dbi = _dbis[portfolid_id];
  } else {
      if (portfolid_id) {
          char str_id[2] = { 0 };
          str_id[0] = portfolid_id;
          ER(mdb_dbi_open(txn, str_id, MDB_CREATE, &dbi));
      }
      else {
          ER(mdb_dbi_open(txn, nullptr, MDB_CREATE, &dbi));
      }
      _dbis[portfolid_id] = dbi;
  }
  return dbi;
}

int BrokerSubSystem::Buy(symbol_t symbol, const Order& order, DealDetail& detail) {
  DealInfo deal;
  if (is_stock(symbol)) {
    return BuyStock(symbol, order, deal);
  }
  WARN("commission is not set");
  return OrderStatus::None;
}

int BrokerSubSystem::Sell(symbol_t symbol, const Order& order, DealDetail& detail) {
  // _orders[symbol].emplace_back(order);
  DealInfo deal;
  if (is_stock(symbol)) {
    return SellStock(symbol, order, deal);
  }
  return OrderStatus::None;
}

double BrokerSubSystem::GetProfitLoss() {
  return 0;
}

void BrokerSubSystem::SetStockCommission(const Commission& comm) {
  _stock = comm;
}

uint32_t BrokerSubSystem::Statistic(float confidence, int N, std::shared_ptr<DataGroup> group, nlohmann::json& indexes) {
    auto var = VaR(confidence);
    indexes["var"] = var;

    auto es = ES(var);
    indexes["es"] = es;
    return 0;
}

void BrokerSubSystem::InitPortfolio(MDB_txn* txn, MDB_dbi dbi) {
  String portfolioName("portfolio");
  auto jsn = LoadJson(portfolioName, txn, dbi);
  if (jsn.empty())
    return;

  for (auto& item: jsn) {
    _portfolio->AddPortfolio(item);
  }
}

nlohmann::json BrokerSubSystem::GetPortfolioJson() {
  nlohmann::json result;
  auto allIDs = _portfolio->GetAllPortfolio();
  for (auto id: allIDs) {
    auto& portfolio = _portfolio->GetPortfolio(id);

    nlohmann::json port;
    port["id"] = id;
    port["pool"] = portfolio._pools;
    port["principal"] = portfolio._principal;
    for (auto& item: portfolio._holds) {
      nlohmann::json asset;
      asset["symbol"] = item.second._symbol;
      asset["count"] = item.second._hold;
      // asset["price"] = item._;

      port["asset"].emplace_back(std::move(asset));
    }
    result.emplace_back(std::move(port));
  }
  return result;
}

nlohmann::json BrokerSubSystem::GetBrokers() {
  nlohmann::json result;
  result["principal"] = _principal;
  return result;
}

void BrokerSubSystem::InitBrokers(MDB_txn* txn, MDB_dbi dbi) {
  String brokerName("broker");
  auto jsn = LoadJson(brokerName, txn, dbi);
  if (jsn.empty())
    return;

  if (jsn.count("principal")) {
    _principal = (double)jsn["principal"];
  }
}

void BrokerSubSystem::InitHistory(MDB_txn* txn, MDB_dbi dbi) {
  String brokerName("history");
  auto jsn = LoadJson(brokerName, txn, dbi);
  if (jsn.empty())
    return;

  for (auto& item: jsn) {
    String symbol = item["symbol"];
    auto symb = to_symbol(symbol);
    auto& trans = _trans[symb];
    std::unique_lock<std::mutex> lock(trans._mtx);
  }
}

nlohmann::json BrokerSubSystem::GetHistoryJson() {
  nlohmann::json jsn;
  for (auto& item: _trans) {
    nlohmann::json temp;
    temp["symbol"] = get_symbol(item.first);
    std::unique_lock<std::mutex> lock(item.second._mtx);
    for (auto& trans: item.second._transactions) {
      nlohmann::json action;
      action["order"]["number"] = trans._order._number;
      for (int i = 0; i < MAX_ORDER_SIZE; ++i) {
        action["order"]["time"].push_back(trans._order._order[i]._time);
        action["order"]["price"].push_back(trans._order._order[i]._price);
      }
      for (auto& deal: trans._deal._deals) {
        action["deal"]["time"].push_back(deal._time);
        action["deal"]["price"].push_back(deal._price);
        action["deal"]["num"].push_back(deal._number);
      }
      temp.emplace_back(std::move(action));
    }
    jsn.emplace_back(std::move(temp));
  }
  return jsn;
}

nlohmann::json BrokerSubSystem::LoadJson(const String& name, MDB_txn* txn, MDB_dbi dbi) {
  MDB_val key; 
  key.mv_data = (void*)name.data();
  key.mv_size = name.size();
  MDB_val data;
  int rc = mdb_get(txn, dbi, &key, &data);
  if (rc != 0)
      return nullptr;
  String cbor((char*)data.mv_data, data.mv_size);
  return nlohmann::json::from_cbor(cbor);
}

void BrokerSubSystem::SaveJson(const String& name, MDB_txn* txn, MDB_dbi dbi, const nlohmann::json& jsn) {
  MDB_val key, data; 
  key.mv_data = (void*)name.data();
  key.mv_size = name.size();

  auto cbor = nlohmann::json::to_cbor(jsn);
  data.mv_data = cbor.data();
  data.mv_size = cbor.size();
  mdb_put(txn, dbi, &key, &data, 0);
}

void BrokerSubSystem::run() {
  MDB_env* _env;
  MDB_txn *_txn;

  ER(mdb_env_create(&_env));
  ER(mdb_env_set_maxreaders(_env, 1));
  ER(mdb_env_set_mapsize(_env, DEFAULT_DISK_CACHE_SIZE));
  ER(mdb_env_set_maxdbs(_env, DEFAULT_MAX_SYMBOLS));
  ER(mdb_env_open(_env, _dbpath.c_str(), MDB_CREATE | MDB_NOTLS | MDB_NORDAHEAD | MDB_NOSUBDIR | MDB_NOLOCK | MDB_WRITEMAP, 0664));
  ER(mdb_txn_begin(_env, NULL, MDB_WRITEMAP, &_txn));

  // initialize
  auto dbi = GetDBI(1, _txn);
  InitPortfolio(_txn, dbi);
  InitBrokers(_txn, dbi);
  InitHistory(_txn, dbi);
  InitPrediction(_txn, dbi);

  SetCurrentThreadName("Broker");
  while (!_exit) {
    auto future = std::chrono::system_clock::now() + std::chrono::seconds(5);
    std::unique_lock<std::mutex> lck(_mutex);
    if (_cv.wait_until(lck, future) == std::cv_status::timeout)
      continue;

    // write file
  }
  // flush
  flush(_txn, dbi);

  for (auto& item: _dbis) {
    mdb_dbi_close(_env, item.second);
  }
  mdb_txn_commit(_txn);
  mdb_env_sync(_env, 1);
}

void BrokerSubSystem::flush(MDB_txn* txn, MDB_dbi dbi) {
  auto history = GetHistoryJson();
  SaveJson("history", txn, dbi, history);
  auto portfolios = GetPortfolioJson();
  SaveJson("portfolio", txn, dbi, portfolios);
  auto broker = GetBrokers();
  SaveJson("broker", txn, dbi, broker);
  auto prediction = GetPrediction();
  SaveJson(DB_PREDICTION, txn, dbi, prediction);
}

double BrokerSubSystem::VaR(float confidence)
{
    // 计算最后组合的VaR值
    // 计算期望与方差
    double mu = 0;
    double sigma = 1;
    double p = 0.99; // 概率值
    boost::math::normal_distribution<> norm(mu, sigma); // 均值0，标准差1
    double z = quantile(norm, confidence); // 分位数值
    return -1;
}

double BrokerSubSystem::ES(double var)
{
    // 计算最后组合的ES
    return -1;
}

const Asset& BrokerSubSystem::GetAsset(const String& symbol) {
  auto id = to_symbol(symbol);
  return _portfolio->_portfolios[_portfolio->Default()]._holds.at(id);
}

int BrokerSubSystem::BuyStock(symbol_t symbol, const Order& order, DealInfo& deal) {
  double pos = _portfolio->Position();
  double buy_price = 0;
  if (_handler) {

  } else {
    // 模拟盘
    buy_price = SimulateMatchStockBuyer(symbol, _principal, order, deal);
  }
  if (deal._deals.empty())
    return OrderStatus::None;
  
  // 更新portfolio
  _portfolio->Update(symbol, deal);
  // 保存
  Transaction data;
  deal._direct = true;
  data._deal = deal;
  data._order = order;
  auto& trans = _trans[symbol];
  std::unique_lock<std::mutex> lock(trans._mtx);
  trans._transactions.emplace_back(std::move(data));
  return OrderStatus::All|OrderStatus::Accept;
}

int BrokerSubSystem::SellStock(symbol_t symbol, const Order& order, DealInfo& deal) {
  double sell_price = 0;
  if (_handler) {
    sell_price = _handler->Sell(symbol, order, deal);
  } else {
    // 模拟盘
    sell_price = SimulateMatchStockSeller(symbol, order, deal);
  }
  if (deal._deals.empty())
    return OrderStatus::None;
  // 更新portfolio
  _portfolio->Update(symbol, deal);
  deal._direct = false;
  // 保存
  Transaction data;
  data._deal = deal;
  data._order = order;
  auto& trans = _trans[symbol];
  std::unique_lock<std::mutex> lock(trans._mtx);
  trans._transactions.emplace_back(std::move(data));
  return OrderStatus::All|OrderStatus::Accept;
}

double BrokerSubSystem::SimulateMatchStockBuyer(symbol_t symbol, double principal, const Order& order, DealInfo& deal) {
  // 一手数量
  constexpr short hand = 100;
  double min_value = hand * order._order.front()._price;
  if (principal < min_value) {
    return 0;
  }

  auto number = order._number;
  if (number == 0) {
    auto lower_total_price = _stock._min / _stock._fee;
    number = ceil(lower_total_price / order._order[0]._price);
  }
  int count = number;

  // 当前实盘价格
  auto latest_quote = _exchange->GetQuote(symbol);
  double price = latest_quote._close;
  double total = count * price;
  if (total > principal)
    return 0;

  // TODO: 模拟只能买入部分/不能买入的场景
  
  _principal -= total;

  DealDetail dd;
  dd._price = price;
  dd._number = number;
  dd._time = Now();
  deal._deals.push_back(std::move(dd));
  return price;
}

double BrokerSubSystem::SimulateMatchStockSeller(symbol_t symbol, const Order& order, DealInfo& deal) {
  auto latest_quote = _exchange->GetQuote(symbol);
  double price = latest_quote._close;
  if (price == 0)
    return price;
  // TODO: 模拟卖出一部分/无法卖出的场景

  // 全部卖出的场景
  double total = order._number * price;
  _principal += total;

  DealDetail dd;
  dd._price = price;
  dd._number = order._number;
  dd._time = Now();
  deal._deals.push_back(std::move(dd));

  return price;
}

void BrokerSubSystem::PredictWithDays(symbol_t symb, int N, int op) {
  auto current = Now();
  auto next_date = ToString(current, "%Y-%m-%d");
  auto pred = std::make_pair(next_date, op);
  std::unique_lock<std::mutex> lck(_predMtx);
  _symbolOperation[symb] = pred;
  _predictions[symb].push_back(pred);
}

bool BrokerSubSystem::GetNextPrediction(symbol_t symb, fixed_time_range& tr, int& op) {
  auto cur = Now();
  {
    std::unique_lock<std::mutex> lck(_predMtx);
    auto itr = _symbolOperation.find(symb);
    if (itr == _symbolOperation.end())
      return false;

    if (itr->second.first == cur && (itr->second.second & (int)ContractOperator::Done) == 0) {
      tr = itr->second.first;
      op = itr->second.second;
      return true;
    }
  }
  return false;
}

void BrokerSubSystem::DoneForecast(symbol_t symb, int operation) {
  List<Pair<fixed_time_range, int>>* history = nullptr;
  {
    std::unique_lock<std::mutex> lck(_predMtx);
    history = &_predictions[symb];
    _symbolOperation[symb].second |= (int)ContractOperator::Done;
  }
  auto cur = Now();
  std::unique_lock<std::mutex> lck(_predMtx);
  for (auto ritr = history->rbegin(); ritr != history->rend(); ++ritr) {
    if (ritr->first < cur)
      break;
    if (ritr->first == cur) {
      ritr->second |= (int)ContractOperator::Done;
      break;
    }
  }
}

void BrokerSubSystem::InitPrediction(MDB_txn* txn, MDB_dbi dbi) {
  String predictName(DB_PREDICTION);
  auto jsn = LoadJson(predictName, txn, dbi);
  if (jsn.empty())
    return;

  if (jsn.count(DB_PREDICTION)) {
    /*
     * [
     *  {"000001": [["2025-5-24", 1], ...
     *    ]
     *  }
     * ]
     */
    for (auto& item: jsn[DB_PREDICTION].items()) {
      auto strSymbol = item.key();
      auto symbol = to_symbol(strSymbol);
      for (auto& prediction: item.value()) {
        String pred = prediction[0];
        int operation = prediction[1];
        fixed_time_range tr(pred);
        auto pr = std::make_pair(std::move(tr), operation);
        _predictions[symbol].push_back(std::move(pr));
      }
    }
  }
}

nlohmann::json BrokerSubSystem::GetPrediction() {
  nlohmann::json prediction;
  std::unique_lock<std::mutex> lck(_predMtx);
  for (auto& pred: _predictions) {
    auto strSymbol = get_symbol(pred.first);
    for (auto& item: pred.second) {
      auto pr = std::make_pair(to_string(item.first) , item.second);
      prediction.emplace_back(std::move(pr));
    }
  }
  return prediction;
}

// double VirtualBroker::Buy(const String& symbol, const Order& order) {
//   double cost = order._real_price * order._number;
//   if (_portfolio._principal < cost) {
//     return -1;
//   }

//   auto id = to_symbol(symbol);
//   if (_portfolio._holds.count(id) == 0) {
//       Asset asset;
//       asset._symbol = symbol;
//       asset._hold = order._number;
//       asset._price = order._real_price;
//       _portfolio._holds[id] = std::move(asset);
//   }
//   else {
//       auto& asset = _portfolio._holds[id];
//       auto cur_capital = asset._price * asset._hold;
//       asset._hold += order._number;
//       cur_capital += order._number * order._real_price;
//       if (asset._hold == 0) {
//           asset._price = 0;
//       }
//       else {
//           asset._price = cur_capital / asset._hold;
//       }
//   }
//   if (_portfolio._pools.count(symbol) == 0) {
//     _portfolio._pools.insert(symbol);
//   }
//   _portfolio._principal -= order._real_price* order._number;
  
//   _orders.emplace_back(std::move(make_pair(symbol, order)));
//   return order._real_price;
// }

// double VirtualBroker::Sell(const String& symbol, const Order& order) {
//     auto id = to_symbol(symbol);
//     if (_portfolio._holds.count(id)) {
//         auto& asset = _portfolio._holds[id];
//         int real_sell = std::min((int)asset._hold, order._number);
//         auto cur_capital = asset._price * asset._hold;
//         asset._hold -= real_sell;
//         cur_capital -= real_sell * order._real_price;
//         if (asset._hold) {
//             asset._price = cur_capital / asset._hold;
//         }
//         else {
//             asset._price = 0;
//         }
        
//         _portfolio._principal += order._real_price * real_sell;
//         _orders.emplace_back(std::move(make_pair(symbol, order)));
//         return order._real_price;
//     }
//     return 0;
// }

// const Asset& VirtualBroker::GetAsset(const String& symbol) {
//   auto id = to_symbol(symbol);
//   return _portfolio._holds.at(id);
// }

// uint32_t VirtualBroker::Statistic(float confidence, int N, std::shared_ptr<DataGroup> group, nlohmann::json& indexes) {
//     double value = 0;
//     for (auto& order : _orders) {
//         nlohmann::json data;
//         data["symbol"] = order.first;
//         data["datetime"] = order.second._real_time;
//         data["price"] = order.second._real_price;
//         data["count"] = order.second._number;
//         data["long"] = order.second._buy_sell;
//         indexes["ops"].emplace_back(std::move(data));

//         nlohmann::json profit;
//         profit["datetime"] = order.second._real_time;
//         double cost = order.second._real_price * order.second._number;
//         value += (order.second._buy_sell ? -cost : cost);
//         profit["value"] = value;
//         indexes["profit"].emplace_back(std::move(profit));
//     }
//     auto& start = _orders.front();
//     double fr = _server->GetFreeRate(start.second._real_time);
//     RiskMetric rm(confidence, fr, _portfolio, group);
//     indexes["var"] = rm.ParametricVaR();
//     indexes["es"] = rm.ExpectedShortfall();
//     indexes["sharp"] = 1.0;
//     return 0;
// }