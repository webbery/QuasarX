#pragma once
#include "Callable.h"

class MinMax : public ICallable {
public:
    MinMax(double lower, double upper);

    virtual feature_t operator()(const Map<String, feature_t>& args);
private:
    double _lower;
    double _upper;
    double _inter;
};