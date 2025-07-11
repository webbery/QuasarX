#pragma once
#include "std_header.h"
#include "Feature.h"
#include "Util/system.h"
#include <yas/serialize.hpp>
#include <yas/std_types.hpp>
#include <yas/std_traits.hpp>

struct DataFeatures {
    Vector<FeatureType> _features;
    Vector<symbol_t> _symbols;
    Vector<float> _data;
    YAS_DEFINE_STRUCT_SERIALIZE("DataMessenger", _symbols, _features, _data);
};