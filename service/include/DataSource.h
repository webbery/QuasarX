#pragma once
#include "std_header.h"
#include "Feature.h"
#include "Util/system.h"
#include <yas/serialize.hpp>
#include <yas/std_types.hpp>
#include <yas/std_traits.hpp>

struct DataFeatures {
    Vector<size_t> _features;
    symbol_t _symbol;
    double _price;
    Vector<float> _data;
    Vector<int64_t> _dimesion;
    YAS_DEFINE_STRUCT_SERIALIZE("DataMessenger", _symbol, _features, _price, _data, _dimesion);
};