#pragma once
#include "std_header.h"
#include "Util/system.h"
#include "Util/datetime.h"
#include <ctime>
#include <limits>
#include <yas/serialize.hpp>
#include <yas/std_types.hpp>
#include <yas/std_traits.hpp>
#include "DataHandler.h"

#define REQUEST_POSITION      1
#define REQUEST_ASSET         2
#define REQUEST_ORDERS        3
#define REQUEST_ORDER         4

enum ExchangeType {
    EX_XTP,
    EX_CTP,
    EX_SIM,
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
  time_t _time;
  double _open;
  double _close;

  uint64_t _volume;
  double _value;
  uint64_t _turnover;

  double _high;
  double _low;

  std::array<double, 5> _bidPrice;
  std::array<uint64_t, 5> _bidVolume;

  std::array<double, 5> _askPrice;
  std::array<uint64_t, 5> _askVolume;

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

  virtual AccountPosition GetPosition() = 0;

  virtual AccountAsset GetAsset() = 0;
  
  virtual bool AddOrder(const String& symbol, Order& order) = 0;

  virtual bool UpdateOrder(order_id id) = 0;

  virtual bool CancelOrder(order_id id) = 0;

  virtual OrderList GetOrders() = 0;

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
};