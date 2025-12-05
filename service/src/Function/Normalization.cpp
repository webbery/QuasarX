#include "Function/Normalization.h"

MinMax::MinMax(double lower, double upper)
:_lower(lower), _upper(upper) {
    _inter = _upper - _lower;
}

feature_t MinMax::operator()(const Map<String, feature_t>& args) {
    auto itr = args.begin();
    auto val = std::get<double>(itr->second);
    return (val - _lower)/_inter;
}
