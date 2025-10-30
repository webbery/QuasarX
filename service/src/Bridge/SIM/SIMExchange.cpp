#include "Bridge/SIM/SIMExchange.h"
#include "DataFrame/DataFrameTypes.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "boost/lockfree/queue.hpp"
#include "std_header.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <numeric>
#include <thread>
#include <utility>
#include "yas/detail/type_traits/flags.hpp"
#include "server.h"

StockSimulation::StockSimulation(Server* server)
  :ExchangeInterface(server),_cur_index(0), _worker(nullptr), _cur_id(0)
  ,_finish(false)
{

}

StockSimulation::~StockSimulation() {
    
}

bool StockSimulation::Init(const ExchangeInfo& handle) {
    _org_path = handle._quote_addr;
    String dbpath = handle._local_addr;
    _worker = new std::thread(&StockSimulation::Worker, this);
    return true;
}

bool StockSimulation::Release() {
  if (_worker) {
    _worker->join();
    delete _worker;
    _worker = nullptr;
  }
  _orders.visit_all([](auto&& item) {
      delete item.second;
      });
  return true;
}

bool StockSimulation::Login(){
    _finish = false;
    return true;
}

bool StockSimulation::IsLogin() {
  return !_finish;
}

bool StockSimulation::GetSymbolExchanges(List<Pair<String, ExchangeName>>& info)
{
    return true;
}

bool StockSimulation::GetPosition(AccountPosition& pos){
    return true;
}

AccountAsset StockSimulation::GetAsset(){
    AccountAsset ass;
    return ass;
}

order_id StockSimulation::AddOrder(const symbol_t& symbol, OrderContext* order){
    OrderInfo info;
    info._id = ++_cur_id;
    info._order = order;
    _reports.emplace(info._id, order);
    // _orders.try_emplace_or_visit(symbol, info, [](auto&){
      
    // });
    if (_orders.count(symbol) == 0) {
        std::pair<symbol_t, boost::lockfree::queue<OrderInfo>*> pr;
        pr.first = symbol;
        pr.second = new boost::lockfree::queue<OrderInfo>(MAX_ORDER_PER_SECOND);
        pr.second->push(info);
       _orders.emplace(std::move(pr));
    }
    else {
        _orders.visit(symbol, [&info](auto&& item) {
            item.second->push(info);
            });
    }
    return order_id{ info._id };
}

void StockSimulation::OnOrderReport(order_id id, const TradeReport& report) {
    _reports.visit(id._id, [&report](auto&& value) {
        value.second->_trades._reports.emplace_back(std::move(report));
        value.second->_flag.store(true);
        value.second->_success.store(true);
        value.second->_promise.set_value(true);
        });
}

bool StockSimulation::CancelOrder(order_id id){
    return true;
}

bool StockSimulation::GetOrders(OrderList& ol)
{
    return true;
}

void StockSimulation::SetFilter(const QuoteFilter& filter) {
  _filter = filter;
  if (!std::filesystem::exists(_org_path)) {
    WARN("{} not exist.", _org_path);
    return;
  }
}

void StockSimulation::UseLevel(int level) {
  if (level == 1) {
    for (auto& code : _filter._symbols) {
      LoadT1(code);
    }
  } else {
    // 
    for (auto& code : _filter._symbols) {
      LoadT0(code);
    }
  }
}

#define CACHE_SIZE  2048
void StockSimulation::LoadT1(const String& code) {
    auto symbol = to_symbol(code);
    String subdir, orgdir;
    if (is_stock(symbol)) {
        subdir = "A_hfq";
        orgdir = "AStock";
    }
    auto file_path = _org_path + "/" + subdir + "/" + code + "_hist_data.csv";
    auto primitive_file_path = _org_path + "/" + orgdir + "/" + code + "_hist_data.csv";
    int index = 0;
    std::ifstream ifs, base_fs;
    ifs.open(file_path);
    if (ifs.is_open()) {
        INFO("load {} success", file_path);
        char cache[CACHE_SIZE] = { 0 };
        Vector<time_t> dates;
        Vector<float> open, close, high, low;
        Vector<int64_t> volume;
        Vector<String>& header = _headers[code];
        header.clear();
        while (ifs.getline(cache, CACHE_SIZE)) {
            Vector<String> row;
            split(cache, row, ",");

            if (index++ == 0) {
                header.emplace_back(row[0]);
                header.emplace_back(row[1]);
                header.emplace_back(row[2]);
                header.emplace_back(row[3]);
                header.emplace_back(row[4]);
                header.emplace_back(row[5]);
                // TODO: ask/bid
                continue;
            }
            dates.emplace_back(FromStr(row[0], "%Y-%m-%d %H:%M:%S"));
            open.emplace_back(std::stof(row[1]));
            close.emplace_back(std::stof(row[2]));
            high.emplace_back(std::stof(row[3]));
            low.emplace_back(std::stof(row[4]));
            volume.emplace_back(std::stol(row[5]));
        }
        ifs.close();

        base_fs.open(primitive_file_path);
        if (base_fs.is_open()) {
            // 对齐时间
            base_fs.close();
        }
        if (header.empty())
            return;

        Vector<uint32_t> indexes(index);
        std::iota(indexes.begin(), indexes.end(), 1);
        DataFrame& df = _csvs[code];
        df.load_index(std::move(indexes));
        df.load_column(header[0].c_str(), std::move(dates));
        df.load_column(header[1].c_str(), std::move(open));
        df.load_column(header[2].c_str(), std::move(close));
        df.load_column(header[3].c_str(), std::move(high));
        df.load_column(header[4].c_str(), std::move(low));
        df.load_column(header[5].c_str(), std::move(volume));
    }
    else {
        INFO("load {} fail", file_path);
    }
}

void StockSimulation::LoadT0(const String& code) {
  auto symbol = to_symbol(code);
  String subdir, orgdir;
  if (is_stock(symbol)) {
    subdir = "stock";
  }
  auto file_path = _org_path + "/zh/" + subdir + "/" + code + ".csv";
  int index = 0;
  std::ifstream ifs;
  ifs.open(file_path);
  if (ifs.is_open()) {
    Vector<time_t> dates;
    Vector<float> open, close, high, low;
    Vector<int64_t> volume;
    char cache[CACHE_SIZE] = { 0 };
    Vector<String>& header = _headers[code];
    header.clear();
    while (ifs.getline(cache, CACHE_SIZE)) {
      Vector<String> row;
      split(cache, row, ",");
      
      if (index++ == 0) {
        header.emplace_back(row[0]);
        header.emplace_back(row[1]);
        header.emplace_back(row[2]);
        header.emplace_back(row[3]);
        header.emplace_back(row[4]);
        header.emplace_back(row[5]);
        // TODO: ask/bid
        continue;
      }

      dates.emplace_back(FromStr(row[0], "%Y-%m-%d %H:%M:%S"));
      open.emplace_back(std::stof(row[1]));
      close.emplace_back(std::stof(row[2]));
      high.emplace_back(std::stof(row[3]));
      low.emplace_back(std::stof(row[4]));
      volume.emplace_back(std::stol(row[5]));
    }
    ifs.close();
    if (header.empty())
      return;

    Vector<uint32_t> indexes(index);
    std::iota(indexes.begin(), indexes.end(), 1);
    DataFrame& df = _csvs[code];
    df.load_index(std::move(indexes));
    df.load_column(header[0].c_str(), std::move(dates));
    df.load_column(header[1].c_str(), std::move(open));
    df.load_column(header[2].c_str(), std::move(close));
    df.load_column(header[3].c_str(), std::move(high));
    df.load_column(header[4].c_str(), std::move(low));
    df.load_column(header[5].c_str(), std::move(volume));
  }
}

void StockSimulation::QueryQuotes() {
  // 5s一次，请求一组信息
  _cv.notify_all();
}

double StockSimulation::GetAvailableFunds()
{
    return 1000000;
}

bool StockSimulation::GetCommission(symbol_t symbol, List<Commission>& comms) {
  return true;
}

void StockSimulation::Worker() {
  Publish(URI_RAW_QUOTE, _sock);
  constexpr std::size_t flags = yas::mem | yas::binary;
  _finish = true;
  while (!_server->IsExit()) {
    // notifys
    std::unique_lock<std::mutex> lock(_mx);
    auto status = _cv.wait_for(lock, std::chrono::seconds(5));
    if (status == std::cv_status::timeout) {
      continue;
    }
    if (_csvs.size() == 0) {
        continue;
    }
    for (auto& df : _csvs) {
      auto& header = _headers[df.first];

      auto& datetime = df.second.get_column<time_t>(header[0].c_str());
      auto& open = df.second.get_column<float>(header[1].c_str());
      auto& close = df.second.get_column<float>(header[2].c_str());
      auto& high = df.second.get_column<float>(header[3].c_str());
      auto& low = df.second.get_column<float>(header[4].c_str());
      auto& volume = df.second.get_column<int64_t>(header[5].c_str());

      auto symbol = to_symbol(df.first);
      auto num = df.second.get_index().size();
      if (_cur_index >= num) {
        _finish = true;
        _cur_index = 0;
      }
      QuoteInfo info;
      info._symbol = symbol;
      info._open = open[_cur_index];
      info._close = close[_cur_index];
      info._high = high[_cur_index];
      info._low = low[_cur_index];
      info._volume = volume[_cur_index];
      info._time = datetime[_cur_index];
      
      yas::shared_buffer buf = yas::save<flags>(info);
      if (0 != nng_send(_sock, buf.data.get(), buf.size, 0)) {
        printf("send quote message e fail.\n");
        return;
      }
      _orders.visit(symbol, [&info, &symbol, this] (auto&& que) {
        OrderInfo oif;
        while (que.second->pop(oif)) {
          TradeReport report = OrderMatch(oif._order->_order, info);
          oif._order->_trades._symbol = symbol;
          OnOrderReport(order_id{ oif._id }, report);
        }
      });
    }
    ++_cur_index;
  }
  _finish = true;
  nng_close(_sock);
}

TradeReport StockSimulation::OrderMatch(const Order& order, const QuoteInfo& quote)
{
    TradeReport report;
    report._price = quote._close;
    report._time = quote._time;
    report._quantity = quote._volume;
    return report;
}

