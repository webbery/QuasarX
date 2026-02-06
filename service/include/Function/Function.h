#pragma once
#include "Callable.h"
#include <cstdint>

class MA : public ICallable {
public:
    MA(short count);
    virtual context_t operator()(const Map<String, context_t>& args);

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
    virtual context_t operator()(const Map<String, context_t>& args);

private:
    std::vector<double> _buffer;
    double _alpha;
};

class MACD: public ICallable {
public:
    virtual context_t operator()(const Map<String, context_t>& args);
};

class VWAP: public ICallable {
public:
    virtual context_t operator()(const Map<String, context_t>& args);
};

class STD: public ICallable {
public:
    STD(int32_t count);
    virtual context_t operator()(const Map<String, context_t>& args);
};

/**
 * 回报率计算: log(P(t)/P(t-1))
 */
class Return: public ICallable {
public:
    /**
     * @param count 
     */
    Return(int32_t count);
    virtual context_t operator()(const Map<String, context_t>& args);

private:
    int32_t _cnts;
};

/**
 * R²
*/
class R2: public ICallable {
public:
    R2(int32_t window);
    virtual context_t operator()(const Map<String, context_t>& args);

private:
    int32_t _window;
};