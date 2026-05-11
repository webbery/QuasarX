#pragma once
#include "Bridge/exchange.h"
#include "std_header.h"
#include <boost/lockfree/queue.hpp>
#include <atomic>
#include <shared_mutex>

#define BACKTEST_INITIAL_CAPITAL    500000

/**
 * @brief 订单信息结构（前向声明）
 */
struct OrderInfo {
    size_t _id;
    OrderContext* _order;
};

/**
 * @brief 回测上下文 - 每个回测实例的私有状态
 *
 * 设计目标：
 * 1. 完全隔离：每个回测有自己的时间游标、资金、持仓
 * 2. 轻量级：不包含大数据，只保存状态索引
 * 3. 线程安全：无共享可变状态
 */
class BacktestContext {
public:
    BacktestContext(run_id_t run_id, const String& strategy_name);
    ~BacktestContext();

    // 禁止复制和移动
    BacktestContext(const BacktestContext&) = delete;
    BacktestContext& operator=(const BacktestContext&) = delete;

    // 时间游标 - 每个标的独立推进
    uint32_t getCurIndex(symbol_t symbol) const;
    void setCurIndex(symbol_t symbol, uint32_t index);
    uint32_t incrementCurIndex(symbol_t symbol);

    // 持仓跟踪
    int64_t getPosition(symbol_t symbol) const;
    void setPosition(symbol_t symbol, int64_t qty);
    void adjustPosition(symbol_t symbol, int delta);

    // 资金管理
    double getCapital() const { return _capital; }
    void setCapital(double capital);
    double getAvailableFunds() const;
    void setAvailableFunds(double funds);
    bool tryReserveFunds(double amount);
    void releaseFunds(double amount);

    // 报价缓存（当前 Bar 的数据）
    QuoteInfo* getQuote(symbol_t symbol);
    const QuoteInfo* getQuote(symbol_t symbol) const;
    void setQuote(symbol_t symbol, const QuoteInfo& quote);

    // 订单队列（每个标的独立）
    boost::lockfree::queue<OrderInfo>* getOrCreateOrderQueue(symbol_t symbol);
    boost::lockfree::queue<OrderInfo>* getOrderQueue(symbol_t symbol);

    // 订单报告
    void addOrderReport(size_t order_id, OrderContext* ctx);
    OrderContext* getOrderReport(size_t order_id);

    // 回测元数据
    run_id_t getRunId() const { return _runId; }
    const String& getStrategyName() const { return _strategy_name; }

    // 进度
    double getProgress() const;
    void setTotalBars(size_t bars) { _totalBars = bars; }
    size_t getTotalBars() const { return _totalBars; }

    // 完成状态
    bool isFinished() const { return _finished; }
    void setFinished(bool finished) { _finished = finished; }

    // 共同时间范围（多标的时间对齐）
    time_t getCommonStartTime() const { return _commonStartTime; }
    void setCommonStartTime(time_t t) { _commonStartTime = t; }
    time_t getCommonEndTime() const { return _commonEndTime; }
    void setCommonEndTime(time_t t) { _commonEndTime = t; }

    // 涉及的所有标的
    void addSymbol(symbol_t symbol);
    const Set<symbol_t>& getSymbols() const { return _symbols; }

    // === 每日收益率记录（SoA 布局，零锁，单线程调用）===
    // 预分配容量，避免反复 realloc
    void reserveDailySnapshots(size_t n) {
        _dates.reserve(n);
        _portfolioValues.reserve(n);
    }

    // 当前时间点的调整系数（stepForward 每轮计算并覆盖）
    // 计算方式：当前时间点 后复权价格 / 原始价格
    // 用途：持仓市值 = 数量 × 后复权价格 / 调整系数（归一化到原始价格体系）
    void setCurrentAdjRatio(symbol_t symbol, double ratio) noexcept { _currentAdjRatios[symbol] = ratio; }
    double getCurrentAdjRatio(symbol_t symbol) const noexcept {
        auto it = _currentAdjRatios.find(symbol);
        return (it != _currentAdjRatios.end()) ? it->second : 1.0;
    }

    // 完美转发 emplace，避免拷贝
    void recordDailySnapshot(time_t date, double portfolio_value) noexcept {
        _dates.emplace_back(date);
        _portfolioValues.emplace_back(portfolio_value);
    }

    // 只读访问（无锁，因为 stepForward 单线程调用）
    size_t dailySnapshotCount() const noexcept { return _dates.size(); }
    const Vector<time_t>& getDates() const noexcept { return _dates; }
    const Vector<double>& getPortfolioValues() const noexcept { return _portfolioValues; }

    // 直接 move 取出（BackTestHandler 收集结果时调用，此后不再访问）
    Vector<time_t> takeDates() noexcept { return std::move(_dates); }
    Vector<double> takePortfolioValues() noexcept { return std::move(_portfolioValues); }

    // 存储计算后的日收益率（AgentSubSystem 计算后存入，BackTestHandler 读取）
    void setDailyReturns(Vector<time_t> dates, Vector<double> returns) noexcept {
        _returnDates = std::move(dates);
        _dailyReturns = std::move(returns);
    }
    const Vector<time_t>& getReturnDates() const noexcept { return _returnDates; }
    const Vector<double>& getDailyReturns() const noexcept { return _dailyReturns; }
    Vector<time_t> takeReturnDates() noexcept { return std::move(_returnDates); }
    Vector<double> takeDailyReturns() noexcept { return std::move(_dailyReturns); }

private:
    run_id_t _runId;                    // 回测运行 ID
    String _strategy_name;              // 策略名称

    // 每个标的独立的时间游标
    Map<symbol_t, std::atomic<uint32_t>*> _curIndices;
    mutable std::mutex _indexMtx;

    // 持仓状态
    Map<symbol_t, std::atomic<int64_t>*> _positions;
    mutable std::mutex _positionMtx;

    // 资金状态
    double _capital = 100000.0;
    std::atomic<double> _availableFunds;

    // 当前 Bar 的报价（每个线程私有，无需锁）
    Map<symbol_t, QuoteInfo> _quotes;
    mutable std::mutex _quoteMtx;

    // 订单队列（每个标的独立）
    Map<symbol_t, boost::lockfree::queue<OrderInfo>*> _orderQueues;
    mutable std::mutex _orderQueueMtx;

    // 订单报告
    Map<size_t, OrderContext*> _orderReports;
    mutable std::mutex _orderReportMtx;

    // 进度跟踪
    std::atomic<size_t> _totalBars{0};
    std::atomic<bool> _finished{false};

    // 共同时间范围（多标的时间对齐）
    time_t _commonStartTime = 0;
    time_t _commonEndTime = 0;

    // 涉及的标的列表
    Set<symbol_t> _symbols;
    mutable std::mutex _symbolsMtx;

    // 每日收益率记录（SoA 布局，零锁，单线程调用）
    Vector<time_t> _dates;
    Vector<double> _portfolioValues;

    // 当前时间点调整系数（stepForward 每轮计算并覆盖）
    Map<symbol_t, double> _currentAdjRatios;

    // 计算后的日收益率（AgentSubSystem → BackTestHandler 传递）
    Vector<time_t> _returnDates;
    Vector<double> _dailyReturns;
};
