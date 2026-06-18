#pragma once
#include "Callable.h"

class MinMax : public ICallable {
public:
    MinMax(double lower, double upper, LostType lt = LostType::Delete, FillMethod fm = FillMethod::None);

    virtual context_t operator()(const Map<String, context_t>& args);
private:
    double _lower;
    double _upper;
    double _inter;
};