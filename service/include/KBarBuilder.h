#pragma once
#include "Bridge/exchange.h"
#include "Util/system.h"
#include "std_header.h"

// K-bar 频率
enum class BarFreq : char {
    Min1 = 0,   // 1 分钟
    Min5 = 1,   // 5 分钟
    Min15 = 2,  // 15 分钟
    Min30 = 3,  // 30 分钟
    Hour1 = 4,  // 1 小时
    Hour2 = 5,  // 2 小时
    Hour4 = 6,  // 4 小时
    Day  = 7,   // 日线
    Week = 8,   // 周线
    Month = 9,  // 月线
};

/**
 * @brief 多标的 K-bar 聚合器
 *
 * 接收异步到达的 tick，按频率聚合为 K-bar，并在多标的对齐后产出快照。
 * 由策略引擎拉动调用。
 *
 * 两种输入模式：
 * - Tick 模式（默认，_aggregateOhlc=false）：输入是逐笔报价，OHLC 均由
 *   _close 推导，成交量累加。用于实盘逐 tick 聚合。
 * - Bar 模式（_aggregateOhlc=true）：输入本身已是完整 bar（如回测里的日线），
 *   open/high/low/close 分别取自输入 bar 的对应字段，周内极值由各日 bar 的
 *   high/low 决定。用于 bar→bar 的降频聚合（如日线→周线）。
 *
 * 产出语义为「完成 bar 滞后」：只有进入下一根 bar 时才吐出上一根，
 * 因此 GetSnapshot 返回的永远是已定稿的 bar，不含未走完的当前 bar。
 */
class KBarBuilder {
public:
    KBarBuilder(BarFreq freq, int tolerance_seconds = 5);

    /// 设置输入是否为完整 bar（true 时 OHLC 取自输入 bar 而非 _close）
    void SetAggregateOhlc(bool agg) { _aggregateOhlc = agg; }

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
        // bar 内最后一个 tick 的时间。定稿 bar 的对外时间戳用它，而非 barStart：
        // 周频下 barStart 恒为周一，无法区分「同一周内多次触发」与「进入新周」。
        time_t lastTickTime = 0;
        int isoWeek = -1;        // ISO 周年周号，仅 BarFreq::Week 使用
        double open = 0;
        double high = 0;
        double low = 0;
        double close = 0;
        uint64_t volume = 0;
        // 后复权价：指标计算用。输入带 _adj_* 时聚合 _adj_*，否则回退到原值。
        double adjOpen = 0;
        double adjHigh = 0;
        double adjLow = 0;
        double adjClose = 0;
        bool hasData = false;
    };

    time_t calcBarStart(time_t tickTime) const;
    time_t calcBarEnd(time_t barStart) const;
    int64_t barInterval() const;

    BarState& getOrCreate(symbol_t symbol);
    void startNewBar(BarState& state, time_t barStart, int isoWeek);
    void pushCompleted(const BarState& state);
    QuoteInfo barToQuote(const BarState& state) const;
    void forwardFillBar(BarState& state, const QuoteInfo* prevQuote) const;
    bool getCompletedSnapshot(Map<symbol_t, QuoteInfo>& snapshot);

    BarFreq _freq;
    int _tolerance;
    bool _aggregateOhlc = false;
    Set<symbol_t> _symbols;

    Map<symbol_t, BarState> _barStates;       // 各标的当前 bar
    Map<symbol_t, QuoteInfo> _lastQuotes;     // 各标的上次快照（用于前向填充）
    time_t _lastBarStart = 0;                 // 上次快照的 bar 起始时间
    bool _initialized = false;
};
