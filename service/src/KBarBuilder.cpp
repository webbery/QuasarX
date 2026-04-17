#include "KBarBuilder.h"
#include "Util/datetime.h"

KBarBuilder::KBarBuilder(BarFreq freq, int tolerance_seconds)
    : _freq(freq), _tolerance(tolerance_seconds) {
}

void KBarBuilder::SetSymbols(const Set<symbol_t>& symbols) {
    _symbols = symbols;
    _barStates.clear();
    _lastQuotes.clear();
    _lastBarStart = 0;
    _initialized = false;
    for (auto& sym : _symbols) {
        _barStates[sym] = BarState{sym};
    }
}

const char* KBarBuilder::FreqToString(BarFreq freq) {
    switch (freq) {
        case BarFreq::Min1: return "1min";
        case BarFreq::Min5: return "5min";
        case BarFreq::Day:  return "1day";
        default:            return "unknown";
    }
}

time_t KBarBuilder::calcBarStart(time_t tickTime) const {
    switch (_freq) {
        case BarFreq::Day:
            return tickTime - (tickTime % 86400);
        case BarFreq::Min1:
            return tickTime - (tickTime % 60);
        case BarFreq::Min5:
            return tickTime - (tickTime % 300);
        default:
            return tickTime;
    }
}

time_t KBarBuilder::calcBarEnd(time_t barStart) const {
    return barStart + barInterval();
}

time_t KBarBuilder::barInterval() const {
    switch (_freq) {
        case BarFreq::Day:  return 86400;
        case BarFreq::Min1: return 60;
        case BarFreq::Min5: return 300;
        default:            return 60;
    }
}

KBarBuilder::BarState& KBarBuilder::getOrCreate(symbol_t symbol) {
    auto itr = _barStates.find(symbol);
    if (itr == _barStates.end()) {
        _barStates[symbol] = BarState{symbol};
    }
    return _barStates[symbol];
}

void KBarBuilder::startNewBar(BarState& state, time_t barStart) {
    // 如果当前 bar 有数据，标记为完成
    if (state.hasData) {
        pushCompleted(state);
    }

    state.barStart = barStart;
    state.barEnd = calcBarEnd(barStart);
    state.open = 0;
    state.high = 0;
    state.low = 0;
    state.close = 0;
    state.volume = 0;
    state.hasData = false;
}

void KBarBuilder::pushCompleted(const BarState& state) {
    auto quote = barToQuote(state);
    _lastQuotes[state.symbol] = quote;
}

QuoteInfo KBarBuilder::barToQuote(const BarState& state) const {
    QuoteInfo qi;
    qi._symbol = state.symbol;
    qi._time = state.barStart;
    qi._open = state.open;
    qi._close = state.close;
    qi._high = state.high;
    qi._low = state.low;
    qi._volume = state.volume;
    return qi;
}

void KBarBuilder::forwardFillBar(BarState& state, const QuoteInfo* prevQuote) const {
    if (!prevQuote) return;

    state.open = prevQuote->_close;
    state.high = prevQuote->_close;
    state.low = prevQuote->_close;
    state.close = prevQuote->_close;
    state.volume = 0;
    state.hasData = true;
}

void KBarBuilder::OnTick(const QuoteInfo& tick) {
    auto& state = getOrCreate(tick._symbol);
    time_t tickBarStart = calcBarStart(tick._time);

    // 第一次 tick 或进入新 bar
    if (!_initialized || tickBarStart >= state.barEnd) {
        startNewBar(state, tickBarStart);
    }

    // 更新 OHLCV
    if (!state.hasData) {
        state.open = tick._close;
        state.high = tick._close;
        state.low = tick._close;
    } else {
        if (tick._close > state.high) state.high = tick._close;
        if (tick._close < state.low) state.low = tick._close;
    }
    state.close = tick._close;
    state.volume += tick._volume;
    state.hasData = true;
}

bool KBarBuilder::GetSnapshot(Map<symbol_t, QuoteInfo>& snapshot) {
    if (_symbols.empty()) return false;

    // 找出当前对齐的 bar 起始时间
    // 取所有已初始化 bar 的最小 barStart（即最早完成的 bar）
    time_t minBarStart = 0;
    bool anyNewBar = false;

    for (auto& sym : _symbols) {
        auto itr = _barStates.find(sym);
        if (itr == _barStates.end()) continue;

        const auto& state = itr->second;
        if (!state.hasData && state.barStart == 0) continue;

        if (minBarStart == 0 || state.barStart < minBarStart) {
            minBarStart = state.barStart;
        }
    }

    // 还没有任何 bar 数据
    if (minBarStart == 0) return false;

    // 如果 bar 起始时间没变化，说明还没有新 bar 完成
    if (_initialized && minBarStart == _lastBarStart) return false;

    // 有新 bar 对齐，构建快照
    snapshot.clear();

    for (auto& sym : _symbols) {
        auto itr = _barStates.find(sym);
        if (itr == _barStates.end()) {
            // 不在活跃列表中，前向填充
            auto lastItr = _lastQuotes.find(sym);
            if (lastItr != _lastQuotes.end()) {
                QuoteInfo filled = lastItr->second;
                filled._time = minBarStart;
                snapshot[sym] = filled;
            }
            continue;
        }

        auto& state = itr->second;

        if (state.barStart == minBarStart && state.hasData) {
            // 该标的 bar 正好对齐
            snapshot[sym] = barToQuote(state);
        } else if (state.barStart > minBarStart && state.hasData) {
            // 该标的已进入下一个 bar，取上一个完成的 bar
            auto lastItr = _lastQuotes.find(sym);
            if (lastItr != _lastQuotes.end()) {
                QuoteInfo filled = lastItr->second;
                filled._time = minBarStart;
                snapshot[sym] = filled;
            }
        } else {
            // 该标的仍在当前 bar 中（barStart < minBarStart，不应该发生）
            // 或无数据，前向填充
            auto lastItr = _lastQuotes.find(sym);
            if (lastItr != _lastQuotes.end()) {
                QuoteInfo filled = lastItr->second;
                filled._time = minBarStart;
                snapshot[sym] = filled;
            }
            // 否则完全缺失，跳过
        }
    }

    // 将刚完成的 bar 状态存入 lastQuotes
    for (auto& sym : _symbols) {
        auto itr = _barStates.find(sym);
        if (itr != _barStates.end() && itr->second.barStart == minBarStart) {
            if (itr->second.hasData) {
                _lastQuotes[sym] = barToQuote(itr->second);
            }
        }
    }

    _lastBarStart = minBarStart;
    _initialized = true;

    return !snapshot.empty();
}

void KBarBuilder::Reset() {
    _barStates.clear();
    _lastQuotes.clear();
    _lastBarStart = 0;
    _initialized = false;
}
