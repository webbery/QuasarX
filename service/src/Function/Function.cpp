#include "Function/Function.h"

MA::MA(short count): _count(0), _nextIndex(0), _sum(0.) {
    _buffer.resize(count, 0.0);
}

feature_t MA::operator()(const feature_t& feature) {
    auto value = std::get<double>(feature);
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
    return feature;
}

double MA::average() {
    if (_count == 0) return 0.0;
    return _sum / _count;
}
