#pragma once
#include "Contract/Contract.h"
#include <functional>
#include <map>
#include <vector>
#include <tuple>
#include "Util/system.h"
#include "Util/Volatility.h"
#include <list>


typedef std::string StockInfo;

template <int N, typename ...T>
double extract(const std::tuple<T...>& item) {
  return std::get<N>(item);
}
// A股
class Stock :public Contract {
public:
  Stock();
  ~Stock();

  virtual bool Init(const std::string& path, int prepare_count);

  const std::map<std::string, std::tuple<double, double>>& GetReturnSTD(VolatilityType vt);

  const StockInfo& GetInfo(const std::string& symbol);

  template<typename ...T>
  const std::map<std::string, std::tuple<T...>> Get(VolatilityType vt, const std::string title...) {
    std::map<std::string, std::tuple<T...>> r;
    return r;
  }

  template<int N, typename ...T, typename Lambda=std::function<double(const std::tuple<T...>&)>>
  std::list<std::tuple<std::string, T...>> Sort(const std::map<std::string, std::tuple<T...>>& data, Lambda lambda = extract<N, T...>) {
    std::list<std::tuple<std::string, T...>> result;
    for (auto& item : data) {
      double r = std::get<0>(item.second);
      double std = std::get<1>(item.second);
      double sr = lambda(item.second);
      auto it = std::upper_bound(result.begin(), result.end(), sr,
                              [](double sr, const std::tuple<std::string, T...>& left) {
                                  return std::get<N + 1>(left) < sr;
                              });
      result.insert(it, std::tuple_cat(std::make_tuple(item.first), item.second));
    }
    return result;
  }

  double Simulate(const std::string& symbol, int next_days);

  // 添加一条记录信息
  void AddRecord(time_t tick, const StockRowInfo& row);

  double GetCurrentPrice(const std::string& symbol);

private:
  // 计算最后N日内的日回报率，N = -1表示计算全部数据
  void CalculateReturnAndSTD(int N = -1);

private:
  std::map<std::string, std::vector<StockRowInfo>> _daily;
  // 日回报率及标准差(小于20个工作日的刚上市公司不计算在内)
  std::map<std::string, std::tuple<double, double>> _daily_r_std;

  std::map<std::string, StockInfo> _info;

  VolatilityType _volatility_type;
};
