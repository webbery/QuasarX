#pragma once
#include "Callable.h"

class MinMax : public ICallable {
public:
    MinMax();

    virtual context_t operator()(const Map<String, context_t>& args);
private:
    double _min = std::numeric_limits<double>::max();
    double _max = std::numeric_limits<double>::lowest();
};