#pragma once
#include "Callable.h"

class Garch: public ICallable {
public:
    Garch(int32_t p, int32_t q);
    virtual context_t operator()(const Map<String, context_t>& args);
};