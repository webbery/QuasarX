#include "Bridge/SIM/SIMExchange.h"
#include "DataFrame/DataFrameTypes.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "std_header.h"
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <numeric>
#include "yas/detail/type_traits/flags.hpp"

StockSimulation::StockSimulation(Server* server)
  :ExchangeInterface(server)
{

}

StockSimulation::~StockSimulation() {
    
}

bool StockSimulation::Init(const ExchangeInfo& handle) {
    _org_path = handle._quote_addr;
    String dbpath = handle._local_addr;
    return Publish(URI_SIM_QUOTE, _sock);
}

bool StockSimulation::Release() {
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
      Vector<float> open, close;
      char cache[CACHE_SIZE] = { 0 };
      Vector<String> header;
      while (ifs.getline(cache, CACHE_SIZE)) {
        Vector<String> row;
        split(cache, row, ",");
        if (index++ == 0) {
          header.emplace_back(row[0]);
          header.emplace_back(row[1]);
          header.emplace_back(row[2]);
          continue;
        }

        dates.emplace_back(FromStr(row[0]));
        open.emplace_back(std::stof(row[1]));
        close.emplace_back(std::stof(row[2]));
      }
      ifs.close();

      Vector<uint32_t> indexes(index);
      std::iota(indexes.begin(), indexes.end(), 1);
      DataFrame& df = _csvs[code];
      df.load_index(std::move(indexes));
      df.load_column(header[0].c_str(), std::move(dates));
      df.load_column(header[1].c_str(), std::move(open));
      df.load_column(header[2].c_str(), std::move(close));
    }
  }
}

void StockSimulation::QueryQuotes() {
  // 请求一组信息
  for (auto& df : _csvs) {
    auto symbol = to_symbol(df.first);
    auto num = df.second.get_index().size();
    for (size_t index = 0; index < num; ++index) {
      auto row = df.second.get_row(index);
      QuoteInfo info;
      info._symbol = symbol;
      info._time = row.at<time_t>(0);
      info._open = row.at<double>(1);
      info._close = row.at<double>(2);
      constexpr std::size_t flags = yas::mem | yas::binary;
      yas::shared_buffer buf = yas::save<flags>(info);
      if (0 != nng_send(_sock, buf.data.get(), buf.size, 0)) {
        printf("send quote message fail.\n");
        return;
      }
    }
    
  }
}
