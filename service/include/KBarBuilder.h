#pragma once
#include "Bridge/exchange.h"
#include "Util/system.h"
#include "std_header.h"

// K-bar 频率
enum class BarFreq : char {
    Min1 = 0,   // 1 分钟
    Min5 = 1,   // 5 分钟
    Day  = 2,   // 日线
};

/**
 * @brief 多标的 K-bar 聚合器
 *
 * 接收异步到达的 tick，按频率聚合为 K-bar，并在多标的对齐后产出快照。
 * 由策略引擎拉动调用，不参与回测流程。
 */
class KBarBuilder {
public:
    KBarBuilder(BarFreq freq, int tolerance_seconds = 5);

    // 注册策略涉及的标的列表
    void SetSymbols(const Set<symbol_t>& symbols);

    // 获取标的列表
    const Set<symbol_t>& GetSymbols() const { return _symbols; }

    // 接收 tick，更新 bar 状态
    void OnTick(const QuoteInfo& tick);

    /**
     * @brief 检查是否有新的 bar 对齐快照（拉动式接口）
     * @param[out] snapshot 各标的最新 bar 对齐到同一时间
     * @return true 表示有新快照
     */
    bool GetSnapshot(Map<symbol_t, QuoteInfo>& snapshot);

    // 当前 bar 时间
    time_t CurrentBarTime() const { return _lastBarStart; }

    // 清空状态
    void Reset();

    // 频率转字符串
    static const char* FreqToString(BarFreq freq);

private:
    struct BarState {
        symbol_t symbol;
        time_t barStart = 0;     // bar 起始时间
        time_t barEnd = 0;       // bar 结束时间
        double open = 0;
        double high = 0;
        double low = 0;
        double close = 0;
        uint64_t volume = 0;
        bool hasData = false;
    };

    time_t calcBarStart(time_t tickTime) const;
    time_t calcBarEnd(time_t barStart) const;
    time_t barInterval() const;

    BarState& getOrCreate(symbol_t symbol);
    void startNewBar(BarState& state, time_t barStart);
    void pushCompleted(const BarState& state);
    QuoteInfo barToQuote(const BarState& state) const;
    void forwardFillBar(BarState& state, const QuoteInfo* prevQuote) const;

    BarFreq _freq;
    int _tolerance;
    Set<symbol_t> _symbols;

    Map<symbol_t, BarState> _barStates;       // 各标的当前 bar
    Map<symbol_t, QuoteInfo> _lastQuotes;     // 各标的上次快照（用于前向填充）
    time_t _lastBarStart = 0;                 // 上次快照的 bar 起始时间
    bool _initialized = false;
};
