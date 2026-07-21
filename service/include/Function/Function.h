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

private:
    int32_t _window;
    std::vector<double> _buffer;
    size_t _count;
    size_t _nextIndex;
    double _sum;
    double _sumSq;
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

/**
 * Z-Score 标准化: (x - μ) / σ
 * 用于将数据转换为标准正态分布，常用于配对交易、均值回归策略
 */
class ZScore: public ICallable {
public:
    /**
     * @param window 滚动窗口大小
     */
    ZScore(int32_t window);
    virtual context_t operator()(const Map<String, context_t>& args);

private:
    int32_t _window;
    std::vector<double> _buffer;  // 滑动窗口缓冲区
    size_t _count;                 // 当前已填充数量
    size_t _nextIndex;             // 下一个写入位置
    double _sum;                   // 当前窗口总和
    double _sumSq;                 // 当前窗口平方和
};

/**
 * ATR — Average True Range (平均真实波幅)
 *
 * True Range = max(high - low, |high - prev_close|, |low - prev_close|)
 * ATR = 滑动窗口均值(TR, period)
 *
 * 输入:
 *   args["high"]  — 最高价 (double 或 Vector<double>)
 *   args["low"]   — 最低价
 *   args["close"] — 收盘价
 *
 * 用途: 自适应止损，止损距离 = ATR × multiplier
 */
class ATR : public ICallable {
public:
    ATR(int32_t period);
    virtual context_t operator()(const Map<String, context_t>& args);

private:
    int32_t _period;
    std::vector<double> _trBuffer;
    size_t _count;
    size_t _nextIndex;
    double _sum;
    double _prevClose;
    bool _hasPrev;
};

class Garch: public ICallable {
public:
    Garch(int32_t p, int32_t q);
    virtual context_t operator()(const Map<String, context_t>& args);
};

/**
 * 量价相关性: rolling_corr(log_return, volume_change, window)
 *
 * 输入:
 *   args["price"]   — 收盘价时间序列 (Vector<double>)
 *   args["volume"]  — 成交量时间序列 (Vector<double>)
 *
 * 计算:
 *   ret[i]   = log(close[i] / close[i-1])
 *   vol[i]   = volume[i] / volume[i-1] - 1
 *   vp_corr  = rolling_corr(ret, vol, window)
 *
 * 含义:
 *   ≈ +1: 价量齐升（放量上涨/缩量下跌，趋势健康）
 *   ≈  0: 价量无关（趋势混乱）
 *   ≈ -1: 价量背离（缩量上涨/放量下跌，趋势可疑）
 */
class VPCorr: public ICallable {
public:
    VPCorr(int32_t window);
    virtual context_t operator()(const Map<String, context_t>& args);

private:
    int32_t _window;
    std::vector<double> _retBuf;   // 收益率环形缓冲
    std::vector<double> _volBuf;   // 成交量变化环形缓冲
    size_t _count;
    size_t _nextIndex;
    double _prevClose;
    double _prevVolume;
    bool _hasPrev;
};