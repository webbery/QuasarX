#include "Function/Normalization.h"

MinMax::MinMax(double lower, double upper, LostType lt, FillType ft )
:_lower(lower), _upper(upper) {
    _inter = _upper - _lower;
}

feature_t MinMax::operator()(const Map<String, feature_t>& args) {
    auto itr = args.begin();
    auto val = std::get<Vector<double>>(itr->second);
    return (val.back() - _lower)/_inter;
}
