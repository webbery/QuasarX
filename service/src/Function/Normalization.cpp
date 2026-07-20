#include "Function/Normalization.h"
#include <limits>

MinMax::MinMax()
: _min(std::numeric_limits<double>::max()),
  _max(std::numeric_limits<double>::lowest()) {
}

context_t MinMax::operator()(const Map<String, context_t>& args) {
    auto itr = args.begin();
    double value;
    std::visit([&value](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double>) {
            value = v;
        } else if constexpr (std::is_same_v<T, Vector<double>>) {
            value = v.empty() ? 0.0 : v.back();
        } else {
            value = 0.0;
        }
    }, itr->second);

    // 动态更新最小/最大值
    if (value < _min) _min = value;
    if (value > _max) _max = value;

    double range = _max - _min;
    if (range < 1e-10) {
        return 0.5;  // 所有值相同，返回中间值
    }

    return (value - _min) / range;
}
