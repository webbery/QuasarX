#pragma once
#include "std_header.h"
#include "Util/system.h"
#include "Util/datetime.h"
#include <ctime>
#include <limits>
#include <yas/serialize.hpp>
#include <yas/std_types.hpp>
#include <yas/std_traits.hpp>
#include "DataGroup.h"
#include <boost/unordered/concurrent_flat_map.hpp>

template <typename K, typename V>
using ConcurrentMap = boost::concurrent_flat_map<K, V>;

// 每只合约每秒最大总订单数
#define MAX_ORDER_PER_SECOND    16

#define REQUEST_POSITION      1
#define REQUEST_ASSET         2
#define REQUEST_ORDERS        3
#define REQUEST_ORDER         4

enum ExchangeType {
    EX_XTP,
    EX_CTP,
    EX_SIM,
    EX_HX,
    EX_Unknow
};

struct ExchangeInfo {
  char _local_addr[16];
  char _quote_addr[16];   // 对于仿真环境,该参数为行情数据存储路径
  int _quote_port;        // 对于仿真环境,表示仿真类型:1 - A股，2 - 期货
  char _trade_addr[16];
  int _trade_port;
  char _username[32];
  char _passwd[32];

  char _account[32];    // 资金账号
  char _accpwd[32];     // 资金密码
  // 监管相关
  int _localPort;
};

// 交易手续费
struct alignas(8) Commission {
public:
    /// 买卖方向
    bool _direction: 1;
    float _stamp;
    float _min;
    double _fee;
};

struct order_id {
  union {
    uint64_t _id;
  };
};

struct AccountAsset {
  double total_asset;
  ///可用资金
  double buying_power;
};

inline bool operator < (order_id left, order_id right) {
  return left._id < right._id;
}

struct QuoteInfo {
  symbol_t _symbol;
  time_t _time = 0;
  double _open;
  double _close;

  uint64_t _volume;
  double _value;
  uint64_t _turnover;

  double _high;
  double _low;

  std::array<double, MAX_ORDER_SIZE_LVL2> _bidPrice;
  std::array<uint64_t, MAX_ORDER_SIZE_LVL2> _bidVolume;

  std::array<double, MAX_ORDER_SIZE_LVL2> _askPrice;
  std::array<uint64_t, MAX_ORDER_SIZE_LVL2> _askVolume;

  char _source;   // 数据源
  char _confidence;// 置信度 0-100

  YAS_DEFINE_STRUCT_SERIALIZE("QuoteInfo", _symbol, _time, _open, _close,
    _volume, _value, _turnover, _high, _low, _bidPrice, _bidVolume, _askPrice, _askVolume,
    _source, _confidence);
};

namespace fmt {
  template <>
  struct formatter<QuoteInfo> {
      constexpr auto parse(format_parse_context& ctx) {
          auto it = ctx.begin();
          return it; // 返回解析结束位置
      }

      //
      template <typename FormatContext>
      auto format(const QuoteInfo& quote, FormatContext& ctx) const {
          return format_to(ctx.out(), "Quote[symbol:{} open:{:.4f} close:{:.4f} high:{:.4f} low:{:.4f} volume:{}]",
            get_symbol(quote._symbol), quote._open, quote._close, quote._high, quote._low, quote._volume);
      }
  };
}


class Server;

struct QuoteFilter {
  Set<String> _symbols;
};

class ExchangeInterface {
public:
  ExchangeInterface(Server* server):_server(server) {}
  virtual ~ExchangeInterface() {}

  virtual const char* Name() = 0;

  virtual bool Init(const ExchangeInfo& handle) = 0;

  virtual void SetFilter(const QuoteFilter& filter) = 0;

  virtual bool Release() = 0;

  virtual bool Login() = 0;
  virtual bool IsLogin() = 0;

  virtual bool GetSymbolExchanges(List<Pair<String, ExchangeName>>& info) = 0;

  // 获取当前可用资金
  virtual double GetAvailableFunds() = 0;

  virtual bool GetPosition(AccountPosition&) = 0;

  virtual AccountAsset GetAsset() = 0;
  
  virtual order_id AddOrder(const symbol_t& symbol, OrderContext* order) = 0;

  virtual void OnOrderReport(order_id id, const TradeReport& report) = 0;

  virtual bool CancelOrder(order_id id) = 0;
  // 获取当前尚未完成的所有订单
  virtual bool GetOrders(OrderList& ol) = 0;

  virtual void QueryQuotes() = 0;

  virtual void StopQuery() = 0;

  virtual QuoteInfo GetQuote(symbol_t symbol) = 0;

  Server* GetHandle() { return _server; }

  void SetWorkingRange(char start_hour, char stop_hour, char start_minute, char stop_minute) {
    _range.emplace_back(WorkingRange{start_hour, start_minute, stop_hour, stop_minute});
  }

  bool IsWorking(time_t tick) {
    if (_range.empty())
      return true;
    for (auto& item: _range) {
      if (IsInTimeRange(tick, item._start_hour, item._stop_hour, item._start_minute, item._stop_minute))
        return true;
    }
    return false;;
  }
  
protected:
  Server* _server;

  struct WorkingRange {
    char _start_hour;
    char _start_minute;
    char _stop_hour;
    char _stop_minute;
  };
  Vector<WorkingRange> _range;

  QuoteFilter _filter;

};