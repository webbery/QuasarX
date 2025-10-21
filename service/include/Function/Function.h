#pragma once
#include "std_header.h"

class ICallable {
public:
    virtual ~ICallable(){}
    virtual feature_t operator()(const feature_t& feature) = 0;
};

class MA : public ICallable {
public:
    virtual feature_t operator()(const feature_t& feature);
};