#pragma once
#include "std_header.h"
#include "Util/system.h"
#include "Util/datetime.h"
#include <ctime>
#include <future>
#include <yas/serialize.hpp>
#include <yas/std_types.hpp>
#include <yas/std_traits.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>

template <typename K, typename V>
using ConcurrentMap = boost::concurrent_flat_map<K, V>;

// 每只合约每秒最大总订单数
#define MAX_ORDER_PER_SECOND    16

#define REQUEST_POSITION      1
#define REQUEST_ASSET         2
#define REQUEST_ORDERS        3
#define REQUEST_ORDER         4

#define MAX_ORDER_SIZE  5
// 2级行情
#define MAX_ORDER_SIZE_LVL2  10

// 用户权限
enum class UserPermission : short {
    None = 0,   // 行情,无交易权限
    MainStock = 1,  // 主板股票
    StockOption = 2,    // 股票期权
    StockShort = 4,     // 融资融券
    Future = 8,         // 期货
    FutureOption = 16,  // 期货期权
    SecondStock = 32,   // 创业板股票
    SciTechStock = 64,  // 科创板股票
};

struct OrderDetail {
  double _price;
};

enum class OrderType: char {
    Market, // 市价单(股票/期权)
    Limit,  // 限价单(股票/期权)
    Condition, // 条件单
    Stop,   // 止损单
    // 冰山订单
    // 最优五档成交剩余转撤销
    // 最优五档成交剩余转限价
    // 本方最优
    // 跨日委托
    // 立即成交剩余撤销
    // 
};

// 订单状态
enum class OrderStatus : char {
    OrderUnknow,
    OrderAccept,        // 报单录入成功
    OrderReject,        // 报单录入失败
    OrderPartSuccess,   // 报单部分成交
    OrderSuccess,       // 报单成交
    OrderFail,          // 报单失败
    CancelPartSuccess,  // 撤单部分成功
    CancelSuccess,      // 撤单成功
    CancelFail,         // 撤单失败
    PartSuccessCancel,  // 报单部分成交部分撤单
    OrderCached,        // 预埋单
    PrivilegeReject,    // 无权限
    NetInterrupt,       // 网络中断
};

// 订单有效期
enum OrderTimeValid : char {
    Today,  // 当日有效
    Future, // 指定日期有效
};

// 期权套保类型
enum class OptionHedge : char {
    Speculation,        // 投机
    Arbitrage,          // 套利
    Hedge,              // 套保
    Covered
};

struct Order {
    uint64_t _id;
    symbol_t _symbol;
    uint32_t _volume; //
    OrderType _type;
    // 买卖方向: 0 买入, 1 卖出
    char _side: 1;
    // 开平仓
    char _flag: 1;
    // 是否行权单
    bool _exec: 1;
    OrderTimeValid _validTime;
    OptionHedge _hedge;
    OrderStatus _status;
    // 订单发起时间
    time_t _time;
    Array<OrderDetail, MAX_ORDER_SIZE> _order;
    String _sysID; // 其他SDK数据的交易ID
};

nlohmann::json order2json(const Order& );

struct TradeReport {
    OrderStatus _status;
    // 成交类型  OrderType
    char _type;
    char _side;
    // 交易员代码
    Array<char, 7> _trader_code;
    int _quantity;
    //成交编号 其他SDK数据的交易ID
    String _sysID;
    // 成交价格
    double _price;
    // 成交时间
    time_t _time;
    // 成交总额
    double _trade_amount;
    
    // YAS_DEFINE_STRUCT_SERIALIZE("DealDetail", _number, _status, _price, _time);
};

String to_sse_string(symbol_t symbol, const TradeReport&);

struct TradeInfo {
    symbol_t _symbol;
    List<TradeReport> _reports;
    // YAS_DEFINE_STRUCT_SERIALIZE("DealInfo", _deals);
};

struct Transaction {
    Order _order;
    TradeInfo _deal;
};


struct OrderContext {
  Order _order;
  // 订单交易结果
  TradeInfo _trades;
  // 订单结束标志
  std::atomic_bool _flag = false;
  // 订单成功标志
  std::atomic_bool _success = false;

  std::promise<bool> _promise;
  std::function<void (const TradeReport&)> _callback;

  void Update(const TradeReport& report) {
    if (_callback) {
      _callback(report);
    }
  }
};

// 佣金相关信息
struct FeeInfo {

};

#define GET_SYMBOL(context) context->_order._symbol

template<>
class fmt::formatter<Order>
{
public:
    constexpr auto parse(auto & context) {
        auto it = context.begin(), end = context.end();
        return it;
    }

    auto format(const Order& order, auto &context) const {
        String ot("Market");
        return fmt::format_to(context.out(), "Order[TYPE:{} NUMNER:{}]", ot, order._volume);
    }
};

template<>
class fmt::formatter<TradeReport>
{
public:
    constexpr auto parse(auto& context) {
        auto it = context.begin(), end = context.end();
        return it;
    }

    auto format(const TradeReport& deal, auto &context) const {
        return fmt::format_to(context.out(), "TradeReport[{} NUMBER:{} PRICE:{}]",
            ToString(deal._time), deal._quantity, deal._price);
    }
};

template<>
class fmt::formatter<TradeInfo>
{
public:
    constexpr auto parse(auto& context) {
        auto it = context.begin(), end = context.end();
        return it;
    }

    auto format(const TradeInfo& info, auto& context) const {
        return fmt::format_to(context.out(), "TradeInfo[{} :{}]",
            get_symbol(info._symbol), info._reports);
    }
};

using OrderList = List<Order>;

enum ExchangeType {
    EX_XTP,
    EX_CTP,
    EX_SIM,
    EX_HX,
    EX_Unknow
};

enum class SecurityType: char {
    Stock,
    Option,
    Future,
    Count,
};

struct ExchangeInfo {
  char _local_addr[16];
  char _quote_addr[16];   // 对于仿真环境,该参数为行情数据存储路径
  int _quote_port;        // 对于仿真环境,表示仿真类型:1 - A股，2 - 期货
  char _default_addr[16];
  int _stock_port;
  char _option_addr[16];
  int _option_port;
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
    bool _valid: 1;   
    bool _status: 1; // 是否有效
    /// 买卖方向
    bool _direction: 1;
    // 费率类型: 0-按金额收取比例 1-按面值收取比例 2-按笔收取金额
    char _type: 5;
    float _stamp;
    float _min;
    double _max;
    double _ration;
};

struct alignas(4) order_id {
  uint32_t _id;
  char _sysID[26];
  char _error;
  char _type;   // 0-股票 1-期权 2-期货

  order_id():_id(0), _error(0), _type(0){memset(_sysID, 0, sizeof(_sysID));}
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

  double _upper;
  double _lower;

  std::array<double, MAX_ORDER_SIZE_LVL2> _bidPrice;
  std::array<uint64_t, MAX_ORDER_SIZE_LVL2> _bidVolume;

  std::array<double, MAX_ORDER_SIZE_LVL2> _askPrice;
  std::array<uint64_t, MAX_ORDER_SIZE_LVL2> _askVolume;

  char _source;   // 数据源
  char _confidence;// 置信度 0-100

  YAS_DEFINE_STRUCT_SERIALIZE("QuoteInfo", _symbol, _time, _open, _close,
    _volume, _value, _turnover, _high, _low, _bidPrice, _bidVolume, _askPrice, _askVolume,
    _source, _confidence, _upper, _lower);
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

enum class AccountType {
  MAIN,       // 主帐号
  CANDIDATE,  // 备用帐号
  ACCOUNT_COUNT
};

class ExchangeInterface {
public:
  ExchangeInterface(Server* server):_server(server), _enableOrder(true) {}
  virtual ~ExchangeInterface() {}

  virtual const char* Name() = 0;

  virtual bool Init(const ExchangeInfo& handle) = 0;

  virtual void SetFilter(const QuoteFilter& filter) = 0;

  virtual bool Release() = 0;

  virtual bool Login(AccountType t = AccountType::MAIN) = 0;
  virtual bool IsLogin() = 0;

  virtual void Logout(AccountType t = AccountType::MAIN) = 0;

  virtual bool GetSymbolExchanges(List<Pair<String, ExchangeName>>& info) = 0;

  // 获取当前可用资金
  virtual double GetAvailableFunds() = 0;

  virtual bool GetPosition(AccountPosition&) = 0;

  virtual AccountAsset GetAsset() = 0;
  
  virtual order_id AddOrder(const symbol_t& symbol, OrderContext* order) = 0;

  virtual void OnOrderReport(order_id id, const TradeReport& report) = 0;

  virtual Boolean CancelOrder(order_id id, OrderContext* order) = 0;
  // 获取当前尚未完成的所有订单
  virtual bool GetOrders(SecurityType type, OrderList& ol) = 0;
  // 查询订单
  virtual bool GetOrder(const String& sysID, Order& ol) = 0;
  // 设置是否允许报单
  virtual void EnableInsertOrder(bool isEnable) {
      _enableOrder = isEnable;
  }
  virtual bool GetEnableOrderStatus() {
      return _enableOrder;
  }

  virtual void QueryQuotes() = 0;

  virtual void StopQuery() = 0;

  virtual QuoteInfo GetQuote(symbol_t symbol) = 0;

  virtual bool GetCommission(symbol_t symbol, List<Commission>& comms) = 0;
  // 权限查询
  virtual Boolean HasPermission(symbol_t symbol) = 0;
  // 换日时，重置各种限流参数等操作
  virtual void Reset() = 0;
  // type = 1-表示每日下单上限 2-表示每秒下单上限 3-表示每秒撤单上限
  virtual int GetStockLimitation(char type) = 0;
  // 设置订单上限，如果超过最大上限则返回失败，如果limitation设置为0则约定按最大值设置
  virtual bool SetStockLimitation(char type, int limitation) = 0;

  virtual void GetFee(FeeInfo& fee, symbol_t symbol={}) = 0;

  Server* GetHandle() { return _server; }
  // 设置工作时间段
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
  bool _enableOrder;    // 是否允许报单
};