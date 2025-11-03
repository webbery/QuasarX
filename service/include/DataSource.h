#pragma once
#include "std_header.h"
#include "Feature.h"
#include "Util/system.h"
#include <cstring>
#include <yas/serialize.hpp>
#include <yas/std_types.hpp>
#include <yas/std_traits.hpp>

struct DataFeatures {
    symbol_t _symbol;
    Vector<String> _names;
    Vector<feature_t> _data;
    Vector<char> _type; // 0-double, 1-vector, 2-matrix
    YAS_DEFINE_STRUCT_SERIALIZE("DataMessenger", _symbol, _names,  _data, _type);
};

namespace yas {
template<typename Archive>
void serialize(Archive& ar, Eigen::MatrixXd& matrix) {
    if constexpr (yas::is_readable_archive<Archive>::value) { // 反序列化
        int32_t rows, cols;
        ar & rows & cols;
        Vector<double> v(rows * cols);
        ar & v;
        matrix.resize(rows, cols);
        memcpy(matrix.data(), v.data(), rows * cols * sizeof(Eigen::MatrixXd::Scalar));
    } else {
        int32_t rows = matrix.rows();
        int32_t cols = matrix.cols();
        ar & rows & cols;
        Vector<Eigen::MatrixXd::Scalar> v(rows * cols);
        memcpy(&(v[0]), matrix.data(), v.size() * sizeof(Eigen::MatrixXd::Scalar));
        ar & v;
    }
}
}