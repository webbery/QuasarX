#include "Bridge/SIM/SIMExchange.h"
#include "DataFrame/DataFrameTypes.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
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
#include "yas/detail/type_traits/flags.hpp"
#include "server.h"

StockSimulation::StockSimulation(Server* server)
  :ExchangeInterface(server),_cur_index(0), _worker(nullptr)
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
  return true;
}

bool StockSimulation::Login(){
    return true;
}

bool StockSimulation::IsLogin() {
  return true;
}

AccountPosition StockSimulation::GetPosition(){
    AccountPosition pos;
    return pos;
}

AccountAsset StockSimulation::GetAsset(){
    AccountAsset ass;
    return ass;
}

bool StockSimulation::AddOrder(const String& symbol, Order& order){
  
    return true;
}

bool StockSimulation::UpdateOrder(order_id id){
    return true;
}

bool StockSimulation::CancelOrder(order_id id){
    return true;
}

OrderList StockSimulation::GetOrders(){
    OrderList ols;
    return ols;
}

void StockSimulation::SetFilter(const QuoteFilter& filter) {
  _filter = filter;
  if (!std::filesystem::exists(_org_path)) {
    WARN("{} not exist.", _org_path);
    return;
  }
#define CACHE_SIZE  2048
  // 初始化数据
  for (auto& dir_entry : std::filesystem::directory_iterator(_org_path)) {
    String name = dir_entry.path().stem().string();

    List<String> tokens;
    split(name, tokens, "_");
    String code = tokens.front();
    if (!_filter._symbols.empty()) {
      if (_filter._symbols.count(code) == 0)
        continue;
    }
    auto file_path = dir_entry.path().string();
    // df.read(file_path.c_str(), hmdf::io_format::csv);

    int index = 0;
    std::ifstream ifs;
    ifs.open(file_path);
    if (ifs.is_open()) {
      Vector<time_t> dates;
      Vector<float> open, close, high, low;
      Vector<int64_t> volume;
      char cache[CACHE_SIZE] = { 0 };
      Vector<String>& header = _headers[code];
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
          // ask/bid
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
}

void StockSimulation::QueryQuotes() {
  // 5s一次，请求一组信息
  _cv.notify_all();
}

void StockSimulation::Worker() {
  Publish(URI_RAW_QUOTE, _sock);
  constexpr std::size_t flags = yas::mem | yas::binary;

  while (!_server->IsExit()) {
    // notifys
    std::unique_lock<std::mutex> lock(_mx);
    auto status = _cv.wait_for(lock, std::chrono::seconds(5));
    if (status == std::cv_status::timeout) {
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
      // DEBUG_INFO("sim send {}", info);
      yas::shared_buffer buf = yas::save<flags>(info);
      if (0 != nng_send(_sock, buf.data.get(), buf.size, 0)) {
        printf("send quote message fail.\n");
        return;
      }
    }
    ++_cur_index;
  }
  nng_close(_sock);
}
