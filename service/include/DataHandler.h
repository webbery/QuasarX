#pragma once
#include "std_header.h"
#include "DataFrame/DataFrame.h"
#include <yas/serialize.hpp>
#include <yas/std_types.hpp>
#include <yas/std_traits.hpp>
#include "ql/math/matrix.hpp"

#define MAX_ORDER_SIZE  5

struct StockOrderDetail {
  time_t _time;
  double _price;
  YAS_DEFINE_STRUCT_SERIALIZE("StockOrderDetail", _time, _price);
};

struct Order {
  int _number;
  Array<StockOrderDetail, MAX_ORDER_SIZE> _order;

  YAS_DEFINE_STRUCT_SERIALIZE("Order", _number, _order);
};

struct DealDetail {
    int _number;
    double _price;
    time_t _time;
    YAS_DEFINE_STRUCT_SERIALIZE("DealDetail", _number, _price, _time);
};

struct DealInfo {
    List<DealDetail> _deals;
    YAS_DEFINE_STRUCT_SERIALIZE("DealInfo", _deals);
};

struct Transaction {
    Order _order;
    DealInfo _deal;
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

    QuantLib::Matrix Correlation(const List<String>& symbols);

    size_t Size(const String& symbol);

    void AddRecord();
    
private:
    bool MoveNext();
private:
    uint32_t _offset;
    Map<String, DataFrame*> _frames;
};