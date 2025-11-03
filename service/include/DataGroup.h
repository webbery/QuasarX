#pragma once
#include "std_header.h"
#include "DataFrame/DataFrame.h"
#include <future>
#include <yas/serialize.hpp>
#include <yas/std_types.hpp>
#include <yas/std_traits.hpp>
#include "fmt/core.h"
#include "Util/datetime.h"
#include "Util/system.h"

#define MAX_ORDER_SIZE  5
// 2级行情
#define MAX_ORDER_SIZE_LVL2  10

struct OrderDetail {
  double _price;
};

enum class OrderType: char {
    Market, // 市价单
    Limit,  // 限价单
    Condition, // 条件单
    // 止损单
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
    NetInterrupt,       // 网络中断
};

// 订单有效期
enum OrderTimeValid : char {
    Today,  // 当日有效
    Future, // 指定日期有效
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
    OrderStatus _status;
    // 订单发起时间
    time_t _time;
    Array<OrderDetail, MAX_ORDER_SIZE> _order;
    String _sysID; // 其他SDK数据的交易ID
    //   YAS_DEFINE_STRUCT_SERIALIZE("Order", _number, _type, _status, _order);
};

nlohmann::json order2json(const Order& );

struct TradeReport {
    OrderStatus _status;
    // 成交类型
    char _type;
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
using RowElementType = Tuple<String, int, double>;

typedef hmdf::StdDataFrame<uint32_t> DataFrame;
typedef DataFrame::View DataView;
typedef hmdf::HeteroVector<DataFrame::align_value> DataFrameRow;
// DataFrame::get_row<uint32_t>();

class DataGroup {
public:
    friend class VaRHandler;
    DataGroup(const List<String>& symbols, Map<String, DataFrame>& data) :_offset(0) {
        for (auto& symbol : symbols) {
            if (data.count(symbol) == 0)
                continue;
            _frames[symbol] = &data.at(symbol);
        }
    }

    bool IsValid();

    List<String> GetAllSymbols();

    template<typename T>
    T Get(const String& symbol, const String& column, int index = 0) {
        auto pos = std::max((uint32_t)0, _offset + index);
        return _frames[symbol]->get_column<T>(column.c_str())[pos];
    }

    template<typename T>
    DataFrame::ColumnVecType<T> GetAll(const String& symbol, const String& column) {
        return _frames[symbol]->get_column<T>(column.c_str());
    }

    /**
     * @brief 获取截面数据的z score
     * 
     * @param column 
     * @param index 
     * @return T 
     */
    Vector<double> GetZScore(const String& column, int index = 0);

    double Sigma(const String& symbol, int Nday = 21);

    Vector<double> Return(const String& symbol, int Nday);

    // Eigen::MatrixXd Correlation(const List<String>& symbols);

    size_t Size(const String& symbol);

    void AddRecord();
    
private:
    bool MoveNext();
private:
    uint32_t _offset;
    Map<String, DataFrame*> _frames;
};