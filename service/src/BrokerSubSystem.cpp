#include "BrokerSubSystem.h"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <utility>
#include <yas/serialize.hpp>
#include "Bridge/exchange.h"
#include "DataGroup.h"
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
#include "Metric/Sharp.h"

#define ER(expr) {int rc = 0; CHECK((rc = (expr)) == MDB_SUCCESS, #expr);  }
#define CHECK(test, msg) ((test) ? (void)0 : ((void)fprintf(stderr, \
	"%s:%d: [%d] %s: %s\n", __FILE__, __LINE__, rc, msg, mdb_strerror(rc)), abort()))

// 1G for disk
#define DEFAULT_DISK_CACHE_SIZE 1073741824
#define DEFAULT_MAX_SYMBOLS     16

#define DB_PREDICTION   "predict"
#define DB_TRADE_NAME   "trade"
#define DB_ORDER_NAME   "O"
#define DB_TRANSACTION_NAME "T"
#define DB_QUANTITY_NAME "q"
#define DB_TIME_NAME "t"
#define DB_PRICE_NAME "p"

Map<ExchangeType, ExchangeInterface*> BrokerSubSystem::_exchanges;

StockCommission::StockCommission() {

}

double StockCommission::GetCommission(symbol_t symbol, int64_t size) {
  if (!is_stock(symbol)) {
    WARN("{} is not a stock.", get_symbol(symbol));
    return -1;
  }

  if (size > 0) {
    return std::max(_min, size * _fee);
  }
  else if (size < 0) {
    return std::max(_min, size * _fee);
  }
  return -1;
}

BrokerSubSystem::BrokerSubSystem(Server* server, bool is_simulation)
    :_thread(nullptr), _exit(false), _order_queue(256), _simulation(is_simulation), _server(server){
    _portfolio = server->GetPortforlioSubSystem();
}

bool BrokerSubSystem::Init(const nlohmann::json& config, const Map<ExchangeType, ExchangeInterface*>& brokers, double principal) {
  String dbpath = config["db"];
  if (_simulation) {
    auto real_path = dbpath + "/" + (String)config["name"] + ".db";
    _dbpath = real_path;
  } else {
    auto virt_path = dbpath + "/virtual.db";
    _dbpath = virt_path;
  }
  if (config["type"] == "stock") {
    if (config.contains("commission")) {
      _stockCommission._min = config["commission"]["min"];
      _stockCommission._ration = config["commission"]["fee"];
    }
  } else {
    WARN("commission is not set for type: {}", String(config["type"]));
    return false;
  }
  
  _principal = principal;
  if (_exchanges.empty()) {
    _exchanges = brokers;
  }

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
  
  _order_queue.consume_all([](OrderContext* ctx) {
    delete ctx;
  });

  for (auto& item: _exchanges) {
    if (item.second)
      item.second->Release();

    delete item.second;
  }
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

order_id BrokerSubSystem::Buy(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& detail) {
    // 检查本金
    return AddOrderBySide(strategy, symbol, order, detail, 0);
}

order_id BrokerSubSystem::Sell(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& detail) {
    // 检查持仓
    return AddOrderBySide(strategy, symbol, order, detail, 1);
}

order_id BrokerSubSystem::Buy(const String& strategy, symbol_t symbol, const Order& order, std::function<void (const TradeReport&)> cb) {
    auto id = AddOrderBySide(strategy, symbol, order, 0, cb);

    return id;
}

order_id BrokerSubSystem::Sell(const String& strategy, symbol_t symbol, const Order& order, std::function<void (const TradeReport&)> cb) {
    auto id = AddOrderBySide(strategy, symbol, order, 1, cb);
    return id;
}

order_id BrokerSubSystem::Exercise(const String& strategy, symbol_t symbol, const Order& order, std::function<void (const TradeReport&)> cb) {
    auto id = AddOrderBySide(strategy, symbol, order, 1, cb);
    return id;
}

double BrokerSubSystem::GetProfitLoss() {
  return 0;
}

void BrokerSubSystem::SetCommission(symbol_t symbol, const Commission& comm) {
  if (is_stock(symbol)) {
    static auto stock_comm = new StockCommission();
    _commissions[symbol] = stock_comm;
  } else {
    WARN("{} commission not support", symbol);
  }
}

bool BrokerSubSystem::QueryOrders(SecurityType type, OrderList& ol)
{
    auto exchange = _server->GetAvaliableStockExchange();
    if (!exchange->GetOrders(type, ol)) {
        WARN("query orders fail");
        return false;
    }
    return true;
}

int BrokerSubSystem::QueryOrder(const String& sysID, Order& order)
{
    auto exchange = _server->GetAvaliableStockExchange();
    if (!exchange->GetOrder(sysID, order)) {
      return 0;
    }
    return 1;
}

void BrokerSubSystem::CancelOrder(order_id& id, symbol_t symbol, std::function<void (const TradeReport&)> cb) {
    auto exchange = _server->GetAvaliableStockExchange();
    OrderContext* ctx = new OrderContext;
    ctx->_flag = false;
    ctx->_callback = cb;
    ctx->_order._symbol = symbol;
    auto ret = exchange->CancelOrder(id, ctx);
    if (!ret.has_value()) {
        if (ret.error() == ERROR_CANCEL_LIMIT) {
            id._error = ERROR_CANCEL_LIMIT;
            delete ctx;
            return;
        }
    }
    _order_queue.push(ctx);
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

float BrokerSubSystem::GetIndicator(const String& name, StatisticIndicator indicator) {
  constexpr double confidence = 0.95;
  switch (indicator) {
  case StatisticIndicator::Sharp:
    // return Sharp(name, -1);
  case StatisticIndicator::VaR:
    return VaR(confidence);
  case StatisticIndicator::ES:
    return ES(VaR(confidence));
  case StatisticIndicator::MaxDrawDown:
  default:
    return 0;
  }
}

StringView BrokerSubSystem::GetIndicatorName(StatisticIndicator indicator) {
  switch (indicator) {
  case StatisticIndicator::Sharp:
    return "sharp";
  case StatisticIndicator::VaR:
    return "VAR";
  case StatisticIndicator::ES:
    return "ES";
  case StatisticIndicator::MaxDrawDown:
    return "MaxDrowDown";
  default:
    return "Unknow";
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
      nlohmann::json hold;
      hold["symbol"] = get_symbol(item.first);
      for (auto& asset: item.second) {
        nlohmann::json data;
        data["quantity"] = asset._quantity;
        data["price"] = asset._price;
        data["time"] = asset._date;
        hold["asset"] = std::move(data);
      }
      // asset["price"] = item._;

      port["hold"].emplace_back(std::move(hold));
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

  for (auto& item : jsn) {
      String symbol = item["symbol"];
      auto symb = to_symbol(symbol);
      // auto& trans = _historyTrades[symb];
      for (auto& action : item[DB_TRANSACTION_NAME]) {
          Transaction tran;
          tran._order._volume = action[DB_ORDER_NAME][DB_QUANTITY_NAME];
          tran._order._time = action[DB_ORDER_NAME][DB_TIME_NAME];
          int i = 0;
          for (double price : action[DB_ORDER_NAME][DB_PRICE_NAME]) {
              tran._order._order[i++]._price = price;
          }
          List<TradeReport> reports;
          for (auto& trade : action[DB_TRADE_NAME]) {
              TradeReport report;
              report._time = trade[DB_TIME_NAME];
              report._price = trade[DB_PRICE_NAME];
              report._quantity = trade[DB_QUANTITY_NAME];
              reports.emplace_back(std::move(report));
          }
          tran._deal._reports = std::move(reports);
      }
  }
}

nlohmann::json BrokerSubSystem::GetHistoryJson() {
  nlohmann::json jsn;
  std::unique_lock<std::mutex> lck(_tradeMtx);
  for (auto& item: _historyTrades) {
    nlohmann::json temp;
    temp["symbol"] = get_symbol(item.first);
    for (auto& trans: item.second) {
      nlohmann::json action;
      action[DB_ORDER_NAME][DB_QUANTITY_NAME] = trans._order._volume;
      action[DB_ORDER_NAME][DB_TIME_NAME] = trans._order._time;
      for (int i = 0; i < MAX_ORDER_SIZE; ++i) {
        action[DB_ORDER_NAME][DB_PRICE_NAME].push_back(trans._order._order[i]._price);
      }
      for (auto& deal : trans._deal._reports) {
          nlohmann::json report;
          report[DB_TIME_NAME] = deal._time;
          report[DB_PRICE_NAME] = deal._price;
          report[DB_QUANTITY_NAME] = deal._quantity;
          action[DB_TRADE_NAME].emplace_back(std::move(report));
      }
      temp[DB_TRANSACTION_NAME].emplace_back(std::move(action));
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
  List<OrderContext*> contexts;
  while (!_exit) {
    auto future = std::chrono::system_clock::now() + std::chrono::seconds(5);
    {
      std::unique_lock<std::mutex> lck(_mutex);
      if (_cv.wait_until(lck, future) == std::cv_status::timeout) {
        flush(_txn, dbi);
        continue;
      }
    }
    

    if (_order_queue.empty() && contexts.empty()) {
      continue;
    }

    while (!_order_queue.empty()) {
      OrderContext* ctx = nullptr;
      if (_order_queue.pop(ctx)) {
          contexts.push_back(ctx);
      }
    }
    for (auto itr = contexts.begin(); itr != contexts.end();) {
        auto ctx = *itr;
        if (ctx->_flag) {
            // TODO: 日志记录
            if (ctx->_success) {
                LOG("Order Success:{}", ctx->_order);
            }
            LOG("Delete Order {}", ctx->_order._id);
            delete ctx;
            itr = contexts.erase(itr);
        }
        else {
            ++itr;
        }
    }
  }
  // flush
  flush(_txn, dbi);

  for (auto& item: _dbis) {
    mdb_dbi_close(_env, item.second);
  }
  mdb_txn_commit(_txn);
  mdb_env_sync(_env, 1);
}

void BrokerSubSystem::RecordTrade(const OrderContext& ctx) {
    auto act = Order2Transaction(ctx);
    std::unique_lock<std::mutex> lck(_tradeMtx);
    _historyTrades[ctx._order._symbol].emplace_back(std::move(act));
}

order_id BrokerSubSystem::AddOrderAsync(OrderContext* order) {

    if (_simulation) {
        return _exchanges[ExchangeType::EX_STOCK_HIST_SIM]->AddOrder(GET_SYMBOL(order), order);
    }
    // 邮件通知
    String content;
    if (order->_order._side == 0) {
        content = std::format("Buy {} {}", get_symbol(GET_SYMBOL(order)), order->_order._order[0]._price);
    } 
    else if (order->_order._side == 1) {
        content = std::format("Sell {} {}", get_symbol(GET_SYMBOL(order)), order->_order._order[0]._price);
    }
    if (!content.empty()) {
        _server->SendEmail(content);
    }
    if (is_stock(GET_SYMBOL(order)) || is_etf_option(GET_SYMBOL(order))) {
        auto exchange = _server->GetAvaliableStockExchange();
        return exchange->AddOrder(GET_SYMBOL(order), order);
    }
    if (is_future(GET_SYMBOL(order))) {
        auto exchange = _server->GetAvaliableFutureExchange();
        return exchange->AddOrder(GET_SYMBOL(order), order);
    }
    order_id id;
    memset(&id, 0, sizeof(order_id));
    return id;  
}

int64_t BrokerSubSystem::AddOrder(symbol_t symbol, const Order& order, std::function<void(const TradeReport&)> cb)
{
    auto ctx = new OrderContext;
    ctx->_order = order;
    GET_SYMBOL(ctx) = symbol;
    ctx->_callback = cb;
    auto id = AddOrderAsync(ctx);
    _order_queue.push(ctx);
    _cv.notify_all();

    // 等待返回
    // auto fut = ctx->_promise.get_future();
    // if (fut.get() == true && cb) {
    //     cb(ctx->_trades);
    // }
    // delete ctx;
    return id._id;
}

void BrokerSubSystem::flush(MDB_txn* txn, MDB_dbi dbi) {
  auto history = GetHistoryJson();
  if (!history.empty()) {
    SaveJson("history", txn, dbi, history);
  }
  auto portfolios = GetPortfolioJson();
  if (!portfolios.empty()) {
    SaveJson("portfolio", txn, dbi, portfolios);
  }
  auto broker = GetBrokers();
  if (!broker.empty()) {
    SaveJson("broker", txn, dbi, broker);
  }
  auto prediction = GetPrediction();
  if (!prediction.empty()) {
    SaveJson(DB_PREDICTION, txn, dbi, prediction);
  }
}

double BrokerSubSystem::VaR(float confidence)
{
    auto& holding = _portfolio->GetHolding();
    if (holding.size() == 1) {
      auto itr = holding.begin();
      auto symbol = itr->first;
      double cost = GetCost(itr->second);
      // 计算最后组合的VaR值
      // 计算期望与方差
      double mu = 0;
      double sigma = 1;
      double p = 0.99; // 概率值
      boost::math::normal_distribution<> norm(mu, sigma); // 均值0，标准差1
      double z = quantile(norm, confidence); // 分位数值
    }
    else if (holding.size() > 1) {

    }
    return -1;
}

double BrokerSubSystem::ES(double var)
{
    // 计算最后组合的ES
    return -1;
}

ICommission* BrokerSubSystem::GetCommision(symbol_t symbol) {
  auto itr = _commissions.find(symbol);
  if (itr == _commissions.end()) {
    SetCommission(symbol, _stockCommission);
    return _commissions[symbol];
  }
  return nullptr;
}

order_id BrokerSubSystem::AddOrderBySide(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& detail, int side)
{
    auto ctx = new OrderContext;
    ctx->_order = order;
    ctx->_order._side = side;
    GET_SYMBOL(ctx) = symbol;
    AddOrderAsync(ctx);
    _order_queue.push(ctx);
    _cv.notify_all();

    // 等待返回
    auto fut = ctx->_promise.get_future();
    // 获取下一次收盘时间
    auto wait_time = 2;
    if (!_simulation) {
      auto exchange = _server->GetExchange(get_symbol(symbol));
      time_t close_t = _server->GetCloseTime(exchange);
      wait_time = Now() - close_t;
    }
    auto future = std::chrono::system_clock::now() + std::chrono::seconds(wait_time);
    
    OrderStatus ret = OrderStatus::OrderUnknow;
    while (std::chrono::system_clock::now() < future) {
      if (fut.wait_for(std::chrono::seconds(1)) == std::future_status::ready) {
        detail = ctx->_trades;
        ret = OrderStatus::OrderSuccess;
        break;
      }
      if (_server->IsExit()) {
        break;
      }
    }
    // 记录
    auto& holds = _portfolio->GetHolding(strategy);
    auto& history = holds[symbol];
    if (side == 0) {
      for (auto& info: detail._reports) {
        history.push_back({static_cast<uint32_t>(info._quantity), info._price, info._time});
      }
    }
    else if (side == 1) {
      // 先进先出
      double org_princpal = 0;
      double total_sell = 0;
      for (auto info: detail._reports) {
        total_sell += info._quantity * info._price;
        while (!history.empty()) {
          auto& front = history.front();
          if (front._quantity >= info._quantity) {
            org_princpal += front._price * info._quantity;
            front._quantity -= info._quantity;
            break;
          } else {
            org_princpal += front._price * front._quantity;
            info._quantity -= front._quantity;
            history.pop_front();
          }
        }
      }
      if (history.empty()) {
        holds.erase(symbol);
      }
      // 损益
      double profit = total_sell - org_princpal;
      _portfolio->UpdateProfit(strategy, profit);
    }
    // 设置结束标志
    ctx->_flag.store(true);
    return order_id();
}

void BrokerSubSystem::ProcessOrderSuccess(const String& strategy, symbol_t symbol, const TradeReport& report) {
    if (report._status == OrderStatus::OrderSuccess) {
        auto& holds = _portfolio->GetHolding(strategy);
        auto& history = holds[symbol];
    }
}

order_id BrokerSubSystem::AddOrderBySide(const String& strategy, symbol_t symbol, const Order& order, int side, std::function<void (const TradeReport&)> cb) {
    auto ctx = new OrderContext;
    ctx->_order = order;
    ctx->_order._side = side;
    GET_SYMBOL(ctx) = symbol;
    ctx->_callback = cb;
    auto id = AddOrderAsync(ctx);
    _order_queue.push(ctx);
    _cv.notify_all();
    return id;
}

double BrokerSubSystem::SimulateMatchStockBuyer(symbol_t symbol, double principal, const Order& order, TradeInfo& deal) {
  // 一手数量
  constexpr short hand = 100;
  double min_value = hand * order._order.front()._price;
  if (principal < min_value) {
    return 0;
  }

  auto number = order._volume;
  if (number == 0) {
    auto lower_total_price = _stockCommission._min / _stockCommission._ration;
    number = ceil(lower_total_price / order._order[0]._price);
  }
  int count = number;
  // 计算预估手续费
  auto comm = GetCommision(symbol);
  auto cost = comm->GetCommission(symbol, count);

  // 当前实盘价格
  auto latest_quote = _exchanges[ExchangeType::EX_STOCK_HIST_SIM]->GetQuote(symbol);
  double price = latest_quote._close;
  double total = count * price + cost;
  if (total > principal)
    return 0;

  // TODO: 模拟只能买入部分/不能买入的场景
  

  TradeReport dd;
  dd._price = price;
  dd._quantity = number;
  dd._time = Now();
  deal._reports.push_back(std::move(dd));
  return price;
}

double BrokerSubSystem::SimulateMatchStockSeller(symbol_t symbol, const Order& order, TradeInfo& deal) {
  auto latest_quote = _exchanges[ExchangeType::EX_STOCK_HIST_SIM]->GetQuote(symbol);
  double price = latest_quote._close;
  if (price == 0)
    return price;
  // TODO: 模拟卖出一部分/无法卖出的场景

  // 全部卖出的场景
  double total = order._volume * price;
  _principal += total;

  TradeReport dd;
  dd._price = price;
  dd._quantity = order._volume;
  dd._time = Now();
  deal._reports.push_back(std::move(dd));

  return price;
}

void BrokerSubSystem::PredictWithDays(symbol_t symb, int N, int op) {
  auto current = Now();
  auto next_date = ToString(current, "%Y-%m-%d");
  auto pred = std::make_pair(next_date, op);
  DEBUG_INFO("PredictWithDays {} {}", next_date, (ContractOperator)op);
  std::unique_lock<std::shared_mutex> lck(_predMtx);
  _symbolOperation[symb] = pred;
  _predictions[symb].push_back(pred);
}

bool BrokerSubSystem::GetNextPrediction(symbol_t symb, fixed_time_range& tr, int& op) {
  auto cur = Now();
  {
    std::shared_lock<std::shared_mutex> lck(_predMtx);
    auto itr = _symbolOperation.find(symb);
    if (itr == _symbolOperation.end())
      return false;

    if (itr->second.first == cur && (itr->second.second & (int)ContractOperator::Done) == 0) {
      tr = itr->second.first;
      op = itr->second.second;
      DEBUG_INFO("GetNextPrediction {}", (ContractOperator)op);
      return true;
    }
  }
  return false;
}

void BrokerSubSystem::DoneForecast(symbol_t symb, int operation) {
  List<Pair<fixed_time_range, int>>* history = nullptr;
  {
    std::shared_lock<std::shared_mutex> lck(_predMtx);
    history = &_predictions[symb];
    _symbolOperation[symb].second |= (int)ContractOperator::Done;
  }
  auto cur = Now();
  std::shared_lock<std::shared_mutex> lck(_predMtx);
  for (auto ritr = history->rbegin(); ritr != history->rend(); ++ritr) {
    if (ritr->first < cur)
      break;
    if (ritr->first == cur) {
      ritr->second |= (int)ContractOperator::Done;
      break;
    }
  }
}

const BrokerSubSystem::predictions_t& BrokerSubSystem::QueryPredictionOfHistory(symbol_t symb) {
  return _predictions[symb];
}

void BrokerSubSystem::DeletePrediction(symbol_t symbol, int index) {
    std::unique_lock<std::shared_mutex> lck(_predMtx);
    auto& preds = _predictions[symbol];
    if (index + 1 == preds.size()) {
      _symbolOperation.erase(symbol);
    }
    int i = 0;
    for (auto itr = preds.begin(); itr != preds.end(); ++itr, ++i) {
      if (i == index) {
        preds.erase(itr);
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
  std::shared_lock<std::shared_mutex> lck(_predMtx);
  for (auto& pred: _predictions) {
    auto strSymbol = get_symbol(pred.first);
    for (auto& item: pred.second) {
      auto pr = std::make_pair(to_string(item.first) , item.second);
      prediction.emplace_back(std::move(pr));
    }
  }
  return prediction;
}

Transaction BrokerSubSystem::Order2Transaction(const OrderContext& context) {
    Transaction act;
    act._order = context._order;
    act._deal = context._trades;
    return act;
}

void BrokerSubSystem::RegistIndicator(const String& strategy, StatisticIndicator indicator) {
  std::unique_lock<std::mutex> lck(_indMtx);
  _indicators[strategy].insert(indicator);
}

void BrokerSubSystem::UnRegistIndicator(const String& strategy, StatisticIndicator indicator) {
  std::unique_lock<std::mutex> lck(_indMtx);
  _indicators[strategy].erase(indicator);
}

void BrokerSubSystem::CleanAllIndicators(const String& strategy) {
  std::unique_lock<std::mutex> lck(_indMtx);
  _indicators.clear();
}

const List<Transaction>& BrokerSubSystem::GetHistoryTrades(symbol_t symbol) const {
  return _historyTrades.at(symbol);
}

Set<symbol_t> BrokerSubSystem::GetPoolSymbols(const String& name) {
  Set<symbol_t> result;
  auto& pool = _portfolio->GetPortfolio(name)._pools;
  for (auto sym: pool) {
    result.insert(to_symbol(sym));
  }
  return result;
}
