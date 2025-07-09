#include "Contract/Stock.h"
#include "Util/Volatility.h"
#include "csv.h"
#include <filesystem>
#include <list>
#include "Util/string_algorithm.h"
#include <ql/quantlib.hpp>

Stock::Stock() {

}

Stock::~Stock() {
}

bool Stock::Init(const std::string& path, int prepare_count) {
  if (!std::filesystem::exists(path))
    return false;

  // 加载基本信息
  auto basic_path = path + "/A_code.csv";
   if (!std::filesystem::exists(basic_path))
    return false;

  io::CSVReader<2> basic_reader(basic_path);
  std::string symbol, name;
  basic_reader.read_header(io::ignore_extra_column, "code", "name");
  while (basic_reader.read_row(symbol, name)) {
#ifdef _WIN32
    _info[format_symbol(symbol)] = to_gbk(name);
#else
    _info[format_symbol(symbol)] = name;
#endif
  }

  // 计算并更新日回报率
  auto daily_path = path + "/A_hfq";
  for (auto& dir_entry: std::filesystem::directory_iterator(daily_path)) {
    std::string name = dir_entry.path().stem().string();
    std::list<std::string> tokens;
    split(name, tokens, "_");
    std::string code = tokens.front();
    _daily[code] = ReadCSV(dir_entry.path().string(), prepare_count);
  }
  
  return true;
}

void Stock::CalculateReturnAndSTD(int N) {
  for (auto& item : _daily) {
    int count = 0;
    int max_idx = (N < 0 ? 0: std::max((int)item.second.size() - N, 0));
    int capcity = (N < 0 ? item.second.size() : N);
    QuantLib::Array prices(capcity);
    for (int i = (int)item.second.size() - 1; i >= max_idx; --i) {
      auto& row = item.second[i];
      // 使用收盘价计算
      auto open_price = std::get<2>(row);
      prices[i - max_idx] = open_price;
      count += 1;
    }
    QuantLib::Array R(capcity - 1);
    double sum = 0;
    for (int i = 1; i < capcity; ++i) {
      double diff_price = prices[i] - prices[i - 1];
      if (prices[i - 1] == 0) {
        R[i - 1] = 0;
      }
      else {
        R[i - 1] = diff_price / prices[i - 1];
      }
      sum += R[i - 1];
    }
    double avg = sum / count;
    auto diff = R - avg;
    auto square = QuantLib::DotProduct(diff, diff);
    double std = std::sqrt(square / (count - 1));
    _daily_r_std[item.first] = std::make_pair(avg, std);
  }
}

const StockInfo& Stock::GetInfo(const std::string& symbol) {
  auto itr = _info.find(symbol);
  if (itr != _info.end())
    return itr->second;
  throw std::string("no stock " + symbol);
}

const std::map<std::string, std::tuple<double, double>>& Stock::GetReturnSTD(VolatilityType vt) {
  CalculateReturnAndSTD();
  return _daily_r_std;
  // TODO: recalculate
}

double Stock::Simulate(const std::string& symbol, int next_days) {
  return 0;
}

void Stock::AddRecord(time_t tick, const StockRowInfo& row) {

}

double Stock::GetCurrentPrice(const std::string& symbol) {
  return 0;
}
