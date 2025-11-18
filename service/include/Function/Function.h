#pragma once
#include "std_header.h"

class ICallable {
public:
    virtual ~ICallable(){}
    virtual feature_t operator()(const feature_t& feature) = 0;
};

class MA : public ICallable {
public:
    MA(short count);
    virtual feature_t operator()(const feature_t& feature);

private:
    std::vector<double> _buffer;
    size_t _count;
    size_t _capacity;
    size_t _nextIndex;
    double _sum;
};