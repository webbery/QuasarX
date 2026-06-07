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
    // 不对齐时，将时间不对齐的 symbol 的 close 填充为 NaN 写入
    for (auto& [symbol, quote] : quotes) {
        if (quote._time == minTime) {
            // 时间对齐：直接写入
            writeFn(quote);
        } else {
            // 时间不对齐：用 NaN 填充 close
            QuoteInfo nanQuote = quote;
            nanQuote._close = std::numeric_limits<double>::quiet_NaN();
            writeFn(nanQuote);
        }
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
