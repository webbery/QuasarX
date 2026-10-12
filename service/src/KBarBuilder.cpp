#include "KBarBuilder.h"
#include "Util/datetime.h"
#include "Util/DuckDBLogger.h"
#include "Util/log.h"

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
        case BarFreq::Min1:  return "1min";
        case BarFreq::Min5:  return "5min";
        case BarFreq::Min15: return "15min";
        case BarFreq::Min30: return "30min";
        case BarFreq::Hour1: return "1hour";
        case BarFreq::Hour2: return "2hour";
        case BarFreq::Hour4: return "4hour";
        case BarFreq::Day:   return "1day";
        case BarFreq::Week:  return "1week";
        case BarFreq::Month: return "1month";
    }
    return "unknown";
}

time_t KBarBuilder::calcBarStart(time_t tickTime) const {
    switch (_freq) {
        case BarFreq::Day:
            return tickTime - (tickTime % 86400);
        case BarFreq::Week:
            // 周频边界由 ISO 周决定，不能用壁钟取模（否则周六/周日断裂）
            return IsoWeekMonday(tickTime);
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

int64_t KBarBuilder::barInterval() const {
    return BarFreqToSeconds(_freq);
}

KBarBuilder::BarState& KBarBuilder::getOrCreate(symbol_t symbol) {
    auto itr = _barStates.find(symbol);
    if (itr == _barStates.end()) {
        _barStates[symbol] = BarState{symbol};
    }
    return _barStates[symbol];
}

void KBarBuilder::startNewBar(BarState& state, time_t barStart, int isoWeek) {
    // 如果当前 bar 有数据，标记为完成
    if (state.hasData) {
        pushCompleted(state);
    }

    state.barStart = barStart;
    state.barEnd = calcBarEnd(barStart);
    state.lastTickTime = 0;
    state.isoWeek = isoWeek;
    state.open = 0;
    state.high = 0;
    state.low = 0;
    state.close = 0;
    state.volume = 0;
    state.adjOpen = 0;
    state.adjHigh = 0;
    state.adjLow = 0;
    state.adjClose = 0;
    state.hasData = false;
}

void KBarBuilder::pushCompleted(const BarState& state) {
    auto quote = barToQuote(state);
    _lastQuotes[state.symbol] = quote;
}

QuoteInfo KBarBuilder::barToQuote(const BarState& state) const {
    QuoteInfo qi;
    qi._symbol = state.symbol;
    // 定稿 bar 的时间戳取该 bar 内最后一个 tick 的时间，而非 barStart。
    // 周频下 barStart 是周一：若用它当 _time，getCompletedSnapshot 的
    // 「minBarStart 变了没有」判据会在周内被同一根 bar 反复触发（每周两次），
    // 且消费方看到的定稿时间早于该周真实收盘日。取 lastTickTime 后，
    // _time 随 bar 内容单调推进，每个定稿 bar 恰好触发一次。
    qi._time = state.lastTickTime > 0 ? state.lastTickTime : state.barStart;
    qi._open = state.open;
    qi._close = state.close;
    qi._high = state.high;
    qi._low = state.low;
    qi._volume = state.volume;
    qi._adj_open = state.adjOpen;
    qi._adj_close = state.adjClose;
    qi._adj_high = state.adjHigh;
    qi._adj_low = state.adjLow;
    return qi;
}

void KBarBuilder::forwardFillBar(BarState& state, const QuoteInfo* prevQuote) const {
    if (!prevQuote) return;

    state.open = prevQuote->_open;
    state.high = prevQuote->_high;
    state.low = prevQuote->_low;
    state.close = prevQuote->_close;
    state.adjOpen = prevQuote->_adj_open;
    state.adjHigh = prevQuote->_adj_high;
    state.adjLow = prevQuote->_adj_low;
    state.adjClose = prevQuote->_adj_close;
    state.volume = 0;
    state.hasData = true;
}

void KBarBuilder::OnTick(const QuoteInfo& tick) {
    auto& state = getOrCreate(tick._symbol);
    time_t tickBarStart = calcBarStart(tick._time);
    int tickIsoWeek = (_freq == BarFreq::Week) ? IsoWeekId(tick._time) : -1;

    // 第一次 tick 或进入新 bar。
    // 首 tick 判定必须用 state.barStart==0，不能用 _initialized：_initialized 同时被
    // getCompletedSnapshot 写，而后者在尚无定稿 bar 时会提前返回、不置位，导致首周内
    // 第二个 tick 再次进 startNewBar，把只含单日数据的半截 bar 当作定稿周线推出。
    if (state.barStart == 0 || tickBarStart > state.barStart || tickBarStart >= state.barEnd) {
        startNewBar(state, tickBarStart, tickIsoWeek);
    }

    // 后复权价缺失时回退到原值，保证 adj 字段始终有效
    double adjOpen = tick._adj_open > 0 ? tick._adj_open : tick._open;
    double adjHigh = tick._adj_high > 0 ? tick._adj_high : tick._high;
    double adjLow = tick._adj_low > 0 ? tick._adj_low : tick._low;
    double adjClose = tick._adj_close > 0 ? tick._adj_close : tick._close;

    if (!_aggregateOhlc) {
        // Tick 模式：输入是逐笔报价，OHLC 均由 _close 推导
        if (!state.hasData) {
            state.open = tick._close;
            state.high = tick._close;
            state.low = tick._close;
            state.adjOpen = adjClose;
            state.adjHigh = adjClose;
            state.adjLow = adjClose;
        } else {
            if (tick._close > state.high) state.high = tick._close;
            if (tick._close < state.low) state.low = tick._close;
            if (adjClose > state.adjHigh) state.adjHigh = adjClose;
            if (adjClose < state.adjLow) state.adjLow = adjClose;
        }
        state.close = tick._close;
        state.adjClose = adjClose;
    } else {
        // Bar 模式：输入本身已是完整 bar（如回测日线→周线）
        if (!state.hasData) {
            state.open = tick._open;
            state.high = tick._high;
            state.low = tick._low;
            state.adjOpen = adjOpen;
            state.adjHigh = adjHigh;
            state.adjLow = adjLow;
        } else {
            if (tick._high > state.high) state.high = tick._high;
            if (tick._low < state.low) state.low = tick._low;
            if (adjHigh > state.adjHigh) state.adjHigh = adjHigh;
            if (adjLow < state.adjLow) state.adjLow = adjLow;
        }
        state.close = tick._close;
        state.adjClose = adjClose;
    }

    state.volume += tick._volume;
    state.lastTickTime = tick._time;
    state.hasData = true;
}

bool KBarBuilder::GetSnapshot(Map<symbol_t, QuoteInfo>& snapshot) {
    if (_symbols.empty()) return false;
    if (_aggregateOhlc) return getCompletedSnapshot(snapshot);

    // 找出当前对齐的 bar 起始时间
    // 取所有已初始化 bar 的最小 barStart（即最早完成的 bar）
    time_t minBarStart = 0;

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
    if (minBarStart == 0) {
        INFO("[KBarBuilder:{}] GetSnapshot: minBarStart=0, returning false", (int)_freq);
        return false;
    }

    // 如果 bar 起始时间没变化，说明还没有新 bar 完成
    if (_initialized && minBarStart == _lastBarStart) {
        // INFO("[KBarBuilder:{}] GetSnapshot: minBarStart={} == _lastBarStart={}, returning false",
        //                (int)_freq, minBarStart, _lastBarStart);
        return false;
    }

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

    DEBUG_INFO("[KBarBuilder:{}] GetSnapshot: _lastBarStart={} -> minBarStart={}, snapshot size={}",
                   (int)_freq, _lastBarStart, minBarStart, snapshot.size());

    _lastBarStart = minBarStart;
    _initialized = true;

    return !snapshot.empty();
}

/**
 * @brief Bar 模式下的快照：只产出已定稿的 bar
 *
 * Tick 模式返回的是「当前正在形成的 bar」（state），因为 tick 场景下调用方要
 * 的是实时值。但降频聚合（如日线→周线）必须返回**已走完的那一根**，否则周频
 * 序列会混入未完成的当周数据，且同一周内值会逐日跳动。
 *
 * pushCompleted 在进入新 bar 时把定稿 bar 写入 _lastQuotes，因此这里直接以
 * _lastQuotes 为准：它天然只含已完成 bar，且天然滞后一根。
 */
bool KBarBuilder::getCompletedSnapshot(Map<symbol_t, QuoteInfo>& snapshot) {
    time_t minBarStart = 0;
    for (auto& sym : _symbols) {
        auto itr = _lastQuotes.find(sym);
        if (itr == _lastQuotes.end()) continue;
        time_t barStart = itr->second._time;
        if (barStart <= 0) continue;
        if (minBarStart == 0 || barStart < minBarStart) {
            minBarStart = barStart;
        }
    }

    if (minBarStart == 0) return false;

    // 还没有新的定稿 bar
    if (_initialized && minBarStart == _lastBarStart) return false;

    snapshot.clear();
    for (auto& sym : _symbols) {
        auto itr = _lastQuotes.find(sym);
        if (itr == _lastQuotes.end()) continue;
        if (itr->second._time == minBarStart) {
            snapshot[sym] = itr->second;
        } else {
            // 该标的本周停牌/未更新：沿用其最近一次定稿值，stamp 到本周
            QuoteInfo filled = itr->second;
            filled._time = minBarStart;
            snapshot[sym] = filled;
        }
    }

    DEBUG_INFO("[KBarBuilder:{}] getCompletedSnapshot: _lastBarStart={} -> minBarStart={}, snapshot size={}",
                   (int)_freq, _lastBarStart, minBarStart, snapshot.size());

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
