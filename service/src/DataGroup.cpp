#include "DataGroup.h"
#include <cstdint>
#include <string>
#include "DataFrame/DataFrameStatsVisitors.h"
#include "DataFrame/DataFrameTypes.h"
#include "boost/math/tools/bivariate_statistics.hpp"
#include "DataFrame/DataFrameFinancialVisitors.h"
#include "Util/string_algorithm.h"

nlohmann::json order2json(const Order& item)
{
    nlohmann::json order;
    order["id"] = item._id;
    order["symbol"] = get_symbol(item._symbol);
    int kind = 0;
    switch (item._symbol._type)
    {
    case contract_type::future:
        kind = 2;
        break;
    case contract_type::put:
    case contract_type::call:
        kind = 1;
        break;
    case contract_type::stock:
    default:
        kind = 0;
        break;
    }
    order["kind"] = kind;
    order["type"] = item._type;
    List<double> prices;
    for (auto detail : item._order) {
        if (detail._price <= 0)
            continue;
        prices.push_back(detail._price);
    }
    order["prices"] = prices;
    order["quantity"] = item._volume;
    order["direct"] = item._side;
    order["status"] = item._status;
    return order;
}

String to_sse_string(const TradeReport& report) {
    String str;
    switch (report._status) {
    case OrderStatus::OrderAccept:
        str += "order_accept";
    break;
    case OrderStatus::OrderReject:
        str += "order_reject";
    break;
    case OrderStatus::OrderSuccess:
    // 状态 id 交易数量 交易价格
        str += "order_success " + String(report._exec_id) + " " + std::to_string(report._quantity) + " " + std::to_string(report._price);
    break;
    case OrderStatus::OrderFail:
        str += "order_fail";
    break;
    case OrderStatus::CancelSuccess:
        str += "cancel_success";
    break;
    case OrderStatus::CancelFail:
        str += "cancel_fail";
    break;
    default:
        str += "unknow";
    break;
    }
    return format_sse("trade_report", {{String("status"), str}});
}

bool DataGroup::IsValid() {
    if (_frames.empty())
        return false;
    return true;
}

bool DataGroup::MoveNext() {
    ++_offset;
    auto itr = _frames.begin();
    auto& v = itr->second->get_index();
    if (_offset < v.size())
        return true;
    // 
    return false;
}

List<String> DataGroup::GetAllSymbols() {
    List<String> symbols;
    std::for_each(_frames.begin(), _frames.end(), [&symbols](const Pair<String, const DataFrame*>& frame) {
        symbols.emplace_back(frame.first);
        });
    return symbols;
}

size_t DataGroup::Size(const String& symbol) {
    if (_frames.count(symbol) == 0)
        return 0;
    
    auto data = _frames.at(symbol);
    return data->get_index().size();
}

// Eigen::MatrixXd DataGroup::Correlation(const List<String>& symbols) {
//     Eigen::MatrixXd m(symbols.size(), symbols.size(), 0);
//     int row = 0;
//     for (auto itr = symbols.begin(); itr != symbols.end(); ++itr) {
//         auto frame1 = _frames[*itr];
//         auto close_data1 = frame1->get_column<double>("close");
//         auto second_itr = itr;
//         ++second_itr;
//         m(row, row) = 1.0;
//         int col = 0;
//         for (; second_itr != symbols.end(); ++second_itr) {
//             auto frame2 = _frames[*itr];
//             auto close_data2 = frame2->get_column<double>("close");
//             double coef = boost::math::tools::correlation_coefficient(close_data1, close_data2);
//             m(col, row) = coef;
//             m(row, col) = coef;
//             ++col;
//         }
//         ++row;
//     }
//     return m;
// }

Vector<double> DataGroup::Return(const String& symbol, int Nday) {
    auto itr = _frames.find(symbol);
    if (itr == _frames.end())
        return Vector<double>();

    hmdf::ReturnVisitor<double> ret_visit(hmdf::return_policy::log, (Nday <= 0? 1: Nday));
    auto& result = itr->second->single_act_visit<double>("close", ret_visit).get_result();
    return result;
}

double DataGroup::Sigma(const String& symbol, int Nday) {
    auto itr = _frames.find(symbol);
    if (itr == _frames.end())
        return 0;

    hmdf::StdVisitor<double> std_visit;
    auto result = itr->second->single_act_visit<double>("close", std_visit).get_result();
    return result;
}

Vector<double> DataGroup::GetZScore(const String& column, int index)
{
    auto pos = std::max((uint32_t)0, _offset + index);
    double mean = 0;
    Vector<double> vals;
    for (auto& item: _frames) {
        auto& val = item.second->get_column<double>(column.c_str())[pos];
        mean += val;
        vals.push_back(val);
    }
    assert(_frames.size() > 0);
    mean = mean/_frames.size();
    double std = 0;
    for (auto val: vals) {
        double t = val - mean;
        std += t*t;
    }
    std = sqrt(std);

    for (auto& val: vals) {
        val = (val - mean) / std;
    }
    return vals;
}

void DataGroup::AddRecord() {

}
