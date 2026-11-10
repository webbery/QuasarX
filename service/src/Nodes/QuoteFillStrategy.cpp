#include "Nodes/QuoteFillStrategy.h"
#include "DataContext.h"
#include "Util/log.h"
#include <algorithm>
#include <limits>

// ============================================================
// SkipFillStrategy
// ============================================================
bool SkipFillStrategy::alignAndWrite(
    const Map<symbol_t, QuoteInfo>& quotes,
    time_t minTime,
    const Map<symbol_t, QuoteInfo>& lastQuotes,
    DataContext& context,
    std::function<void(const QuoteInfo&)> writeFn
) const {
    // 不对齐时，把时间不对齐的标的的当根价格置为 NaN（序列长度保持不变）。
    //
    // 为什么要连**后复权价**一起置 NaN：
    // QuoteInputNode::writeQuote 取的是 "_adj_close > 0 ? _adj_close : _close"，
    // 只置 _close 为 NaN 时发布的 "close" 序列仍然是真值 —— 下游拿到的是一个
    // 「看起来正常」的错日价格，skip 形同虚设。adj_* 置 NaN 后
    // writeQuote 的三元判断（NaN > 0 为 false）会落到 _close，两条路径都是 NaN。
    //
    // 置 NaN 之后下游既有的防护自动生效，不需要额外的开关：
    //   SpreadNode::currentPrices → 任一腿非有限 → 不算价差、且不推进滚动窗口
    //                              （β 不被停牌期间的错日价格污染）
    //   FunctionNode ZScore       → NaN 输入直接返回 NaN 且不入滑动窗口
    //                              （长窗口 z 不会被毒化）
    //   PairNode::Process         → z 非有限 → 本 bar 不动作
    for (auto& [symbol, quote] : quotes) {
        if (quote._time == minTime) {
            // 时间对齐：直接写入
            writeFn(quote);
            continue;
        }
        // 时间不对齐：该标的当根无有效行情（停牌 / 缺数据）→ 价格全部置 NaN
        QuoteInfo nanQuote = quote;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        nanQuote._open = nan;
        nanQuote._high = nan;
        nanQuote._low = nan;
        nanQuote._close = nan;
        nanQuote._adj_open = nan;
        nanQuote._adj_high = nan;
        nanQuote._adj_low = nan;
        nanQuote._adj_close = nan;
        writeFn(nanQuote);
    }
    return true;
}

// ============================================================
// ForwardFillStrategy
// ============================================================
bool ForwardFillStrategy::alignAndWrite(
    const Map<symbol_t, QuoteInfo>& quotes,
    time_t minTime,
    const Map<symbol_t, QuoteInfo>& lastQuotes,
    DataContext& context,
    std::function<void(const QuoteInfo&)> writeFn
) const {
    for (auto& [symbol, quote] : quotes) {
        if (quote._time == minTime) {
            // 时间对齐：直接写入
            writeFn(quote);
        } else {
            // 时间不对齐：用上一个已知值填充
            auto it = lastQuotes.find(symbol);
            if (it != lastQuotes.end()) {
                writeFn(it->second);
            }
        }
    }
    return true;
}

// ============================================================
// LinearFillStrategy
// ============================================================
bool LinearFillStrategy::alignAndWrite(
    const Map<symbol_t, QuoteInfo>& quotes,
    time_t minTime,
    const Map<symbol_t, QuoteInfo>& lastQuotes,
    DataContext& context,
    std::function<void(const QuoteInfo&)> writeFn
) const {
    for (auto& [symbol, quote] : quotes) {
        if (quote._time == minTime) {
            // 时间对齐：直接写入
            writeFn(quote);
        } else {
            // 时间不对齐：用前后 bar 线性插值
            auto it = lastQuotes.find(symbol);
            if (it != lastQuotes.end()) {
                QuoteInfo interpolated = interpolate(it->second, quote, minTime);
                writeFn(interpolated);
            }
        }
    }
    return true;
}

QuoteInfo LinearFillStrategy::interpolate(const QuoteInfo& last, const QuoteInfo& next, time_t target) const {
    time_t range = next._time - last._time;
    if (range == 0) return last;

    double ratio = static_cast<double>(target - last._time) / range;
    ratio = std::max(0.0, std::min(1.0, ratio));

    QuoteInfo result;
    result._symbol = last._symbol;
    result._time = target;
    result._open = last._open + (next._open - last._open) * ratio;
    result._close = last._close + (next._close - last._close) * ratio;
    result._high = last._high + (next._high - last._high) * ratio;
    result._low = last._low + (next._low - last._low) * ratio;
    result._volume = static_cast<int64_t>(last._volume + (next._volume - last._volume) * ratio);
    return result;
}

// ============================================================
// BackwardFillStrategy
// ============================================================
bool BackwardFillStrategy::alignAndWrite(
    const Map<symbol_t, QuoteInfo>& quotes,
    time_t minTime,
    const Map<symbol_t, QuoteInfo>& lastQuotes,
    DataContext& context,
    std::function<void(const QuoteInfo&)> writeFn
) const {
    for (auto& [symbol, quote] : quotes) {
        if (quote._time == minTime) {
            // 时间对齐：直接写入
            writeFn(quote);
        } else {
            // 时间不对齐：用下一个已知值填充（即当前 quote）
            writeFn(quote);
        }
    }
    return true;
}

// ============================================================
// 工厂函数
// ============================================================
std::unique_ptr<IQuoteFillStrategy> CreateFillStrategy(MissingHandleType type) {
    switch (type) {
    case MissingHandleType::Skip:
        return std::make_unique<SkipFillStrategy>();
    case MissingHandleType::ForwardFill:
        return std::make_unique<ForwardFillStrategy>();
    case MissingHandleType::Linear:
        return std::make_unique<LinearFillStrategy>();
    case MissingHandleType::BackwardFill:
        return std::make_unique<BackwardFillStrategy>();
    default:
        return std::make_unique<SkipFillStrategy>();
    }
}
