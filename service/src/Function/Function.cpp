#include "Function/Function.h"

MA::MA(short count): _capacity(count), _count(0), _nextIndex(0), _sum(0.) {
    _buffer.resize(count, 0.0);
}

feature_t MA::operator()(const feature_t& feature) {
    return feature;
}
