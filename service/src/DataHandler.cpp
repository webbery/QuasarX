#include "DataHandler.h"
#include <cstdint>
#include "DataFrame/DataFrameStatsVisitors.h"
#include "DataFrame/DataFrameTypes.h"
#include "boost/math/tools/bivariate_statistics.hpp"
#include "DataFrame/DataFrameFinancialVisitors.h"

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
