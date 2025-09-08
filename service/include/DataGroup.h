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

struct StockOrderDetail {
  double _price;
};

enum class OrderType: char {
    Market, // 市价单
    Limit,  // 限价单
};

// 订单状态
enum OrderStatus: short {
    All = 1,
    Part = 2,
    None = 4,
    Accept = 8,
    Submmit = 16, // 已提交
    Completed = 32,// 已成交
    Margin = 64,  // 保证金不足
    Rejected = 128
};

struct Order {
  uint32_t _number; //
  OrderType _type;
  OrderStatus _status;
  // 买卖方向: 0 买入, 1 卖出
  char _side;
  time_t _time;
  Array<StockOrderDetail, MAX_ORDER_SIZE> _order;

//   YAS_DEFINE_STRUCT_SERIALIZE("Order", _number, _type, _status, _order);
};

enum class DealStatus: char {
    Success,
    Fail,
    NetInterrupt,
};

struct TradeReport {
    int _quantity;
    DealStatus _status;
    // 成交价格
    double _price;
    // 成交时间
    time_t _time;
    // 成交总额
    double _trade_amount;
    //成交编号，深交所唯一，上交所每笔交易唯一，当发现2笔成交回报拥有相同的exec_id，则可以认为此笔交易自成交
    char _exec_id[18];
    // 成交类型
    char _type;
    // 交易员代码
    char _trader_code[7];
    // YAS_DEFINE_STRUCT_SERIALIZE("DealDetail", _number, _status, _price, _time);
};

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
  symbol_t _symbol;
  Order _order;
  // 订单交易结果
  TradeInfo _trades;
  // 订单结束标志
  std::atomic_bool _flag = false;
  // 订单成功标志
  std::atomic_bool _success = false;

  std::promise<bool> _promise;
  std::function<void (Order&)> _callback;

  void Update(Order& order) {
    if (_callback) {
      _callback(order);
    }
  }
};

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
        return fmt::format_to(context.out(), "Order[TYPE:{} NUMNER:{}]", ot, order._number);
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