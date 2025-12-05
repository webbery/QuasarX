#pragma once
#include "std_header.h"

class ICallable {
public:
    virtual ~ICallable(){}
    virtual feature_t operator()(const Map<String, feature_t>& args) = 0;
};
