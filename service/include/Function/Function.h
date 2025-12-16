#pragma once
#include "Callable.h"

class MA : public ICallable {
public:
    MA(short count);
    virtual feature_t operator()(const Map<String, feature_t>& args);

private:
    double average();
private:
    std::vector<double> _buffer;
    size_t _count;
    size_t _nextIndex;
    double _sum;
};

class EMA: public ICallable {
public:
    EMA(short count, double alpha);
    virtual feature_t operator()(const Map<String, feature_t>& args);

private:
    std::vector<double> _buffer;
    double _alpha;
};

class MACD: public ICallable {
public:
    virtual feature_t operator()(const Map<String, feature_t>& args);
};

class VWAP: public ICallable {
public:
    virtual feature_t operator()(const Map<String, feature_t>& args);
};

class STD: public ICallable {
public:
    STD(short count);
    virtual feature_t operator()(const Map<String, feature_t>& args);
};