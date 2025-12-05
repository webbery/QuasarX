#include "Function/Function.h"
#include "Util/datetime.h"
#include <type_traits>
#include <variant>

MA::MA(short count): _count(0), _nextIndex(0), _sum(0.) {
    _buffer.resize(count, 0.0);
}

feature_t MA::operator()(const Map<String, feature_t>& features) {
    if (features.size() != 1) {
        return std::nan("nan");
    }
    auto itr = features.begin();
    double value;
    std::visit([&value] (auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double>) {
            value = v;
        }
        else if constexpr (std::is_same_v<T, Vector<double>>) {
            value = v.back();
        } else {
            INFO("Not support MA");
        }
    }, itr->second);
    if (_count < _buffer.size()) {
        _buffer[_count] = value;
        _sum += value;
        ++_count;
        return average();
    }
    _sum -= _buffer[_nextIndex];
    _buffer[_nextIndex] = value;
    _sum += value;
    _nextIndex = (_nextIndex + 1) % _buffer.size();
    return average();
}

double MA::average() {
    if (_count == 0) return 0.0;
    return _sum / _count;
}

EMA::EMA(short count, double alpha)
:_alpha(alpha) {
    _buffer.resize(count);
}

feature_t EMA::operator()(const Map<String, feature_t>& args) {
    return 0.;
}

feature_t MACD::operator()(const Map<String, feature_t>& args) {

    return 0.;
}