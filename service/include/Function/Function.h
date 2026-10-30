#pragma once
#include "Callable.h"
#include "Algorithms/Kahan.h"
#include <cstdint>
#include <deque>
#include <utility>

// skipNaN 模式的状态（MA / STD 共用）：
//   - _minPeriods: 窗口内至少需要多少个有效值才输出
//   - _ddof:       自由度调整（STD 用，MA 忽略）
//   - _validCount: 窗口内的有效值个数
// 只在 skipNaN=true 时动态分配，旧路径（skipNaN=false）不占用额外内存。
struct SkipNaNState {
    int32_t _minPeriods = 0;
    int32_t _ddof = 0;
    int32_t _validCount = 0;
};

class MA : public ICallable {
public:
    /**
     * @param count      窗口长度（bars）
     * @param skipNaN    true = pandas 语义：窗口按 bar 推进，NaN 不计入统计量
     *                   （false 时保持旧行为：NaN 会污染累加器）
     * @param minPeriods 窗口内至少需要多少个有效值才输出；<=0 表示默认（等于窗口长度）
     */
    MA(int32_t count, bool skipNaN = false, int32_t minPeriods = 0);
    ~MA();
    virtual context_t operator()(const Map<String, context_t>& args);

private:
    double average();
    void recomputeSum();
    context_t applySkipNaN(double value);  // pandas 语义：按 bar 推进窗口、跳过 NaN
    void recomputeValid();                 // skipNaN 模式：只重算有效值
private:
    int32_t _count;
    int32_t _nextIndex;
    int32_t _stepsSinceRecompute; // 定期重算计数器，防止滑动窗口 compensation 漂移
    KahanAccumulator _sumAcc;  // Kahan 补偿求和，降低累积误差
    std::vector<double> _buffer;
    SkipNaNState* _skipState;  // 只在 skipNaN=true 时分配
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
    /**
     * @param count      窗口长度（bars）
     * @param skipNaN    true = pandas 语义：窗口按 bar 推进，NaN 不计入统计量
     * @param minPeriods 窗口内至少需要多少个有效值才输出；<=0 表示默认（等于窗口长度）
     * @param ddof       0 = 总体标准差（旧行为），1 = 样本标准差（pandas rolling().std()）
     */
    STD(int32_t count, bool skipNaN = false, int32_t minPeriods = 0, int32_t ddof = 0);
    ~STD();
    virtual context_t operator()(const Map<String, context_t>& args);

private:
    context_t applySkipNaN(double value);  // pandas 语义：按 bar 推进窗口、跳过 NaN
    int32_t _window;
    int32_t _count;
    int32_t _nextIndex;
    int32_t _stepsSinceRecompute; // 定期重算计数器
    KahanAccumulator _sumAcc;    // Kahan 补偿求和
    KahanAccumulator _sumSqAcc;  // Kahan 补偿平方和
    std::vector<double> _buffer;
    SkipNaNState* _skipState;  // 只在 skipNaN=true 时分配
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
    int32_t _count;                 // 当前已填充数量
    int32_t _nextIndex;             // 下一个写入位置
    int32_t _stepsSinceRecompute;   // 定期重算计数器
    KahanAccumulator _sumAcc;      // Kahan 补偿求和
    KahanAccumulator _sumSqAcc;    // Kahan 补偿平方和
    std::vector<double> _buffer;  // 滑动窗口缓冲区
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
    int32_t _count;
    int32_t _nextIndex;
    bool _hasPrev;
    KahanAccumulator _sumAcc;  // Kahan 补偿求和
    double _prevClose;
    std::vector<double> _trBuffer;
};

class Garch: public ICallable {
public:
    Garch(int32_t p, int32_t q);
    virtual context_t operator()(const Map<String, context_t>& args);
};

/**
 * 滚动窗口中位数: window 内元素的中位数
 *
 * 用法: FunctionNode(method=Median, range=20d)
 *
 * 输出: 窗口内排序后的中位数；偶数窗口时取平均两个中位值
 * 数据不足时: NaN
 *
 * 实现: 滑动窗口 + nth_element 选 kth 元素，O(N) per push
 */
class Median : public ICallable {
public:
    /**
     * @param window 滚动窗口大小（bars 数）
     */
    Median(int32_t window);
    virtual context_t operator()(const Map<String, context_t>& args);

private:
    int32_t _window;
    int32_t _count;       // 当前已填充数量
    int32_t _nextIndex;   // 下一个写入位置（环形缓冲）
    std::vector<double> _buffer;  // 滑动窗口缓冲区
};

/**
 * 区间最高价: rolling_max(price, window)
 *
 * 输入:
 *   args["price"] — 收盘价时间序列 (Vector<double>)，取最新一根 bar
 *
 * 输出: 窗口内的最大值
 * 数据不足时: NaN（与 MA/Median 一致，保证输出长度与输入同步）
 *
 * 用途: 与 close 组合即可算回撤，如 (close[t] / cummax[t] - 1)。
 *
 * 实现: 单调队列（deque），均摊 O(1) per push。
 *   max 不可逆加减，无法像 MA 那样增量 sub/add —— 滑出窗口的元素一旦
 *   正是当前最大值就必须回退，队列法保证这只在必要时发生：
 *   入队时从队尾弹掉所有 ≤ 新值的下标（它们永远不可能再成为最大值），
 *   出队时弹掉已滑出窗口的队首下标。队首恒为窗口内最大值。
 */
class CumMax : public ICallable {
public:
    /**
     * @param window 滚动窗口大小（bars 数）
     */
    CumMax(int32_t window);
    virtual context_t operator()(const Map<String, context_t>& args);

private:
    int32_t _window;
    int32_t _count;       // 已推入的 bar 总数
    // 单调递减队列：存 (下标, 值)，值从队首到队尾严格递减
    std::deque<std::pair<int32_t, double>> _dq;
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
    bool _hasPrev;
    int32_t _window;
    int32_t _count;
    int32_t _nextIndex;
    double _prevClose;
    double _prevVolume;
    std::vector<double> _retBuf;   // 收益率环形缓冲
    std::vector<double> _volBuf;   // 成交量变化环形缓冲
};