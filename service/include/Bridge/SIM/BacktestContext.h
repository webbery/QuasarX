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
    BacktestContext(uint16_t run_id, const String& strategy_name);
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
    uint16_t getRunId() const { return _runId; }
    const String& getStrategyName() const { return _strategy_name; }

    // 进度
    double getProgress() const;
    void setTotalBars(size_t bars) { _totalBars = bars; }
    size_t getTotalBars() const { return _totalBars; }

    // 完成状态
    bool isFinished() const { return _finished; }
    void setFinished(bool finished) { _finished = finished; }

    // 涉及的所有标的
    void addSymbol(symbol_t symbol);
    const Set<symbol_t>& getSymbols() const { return _symbols; }

private:
    uint16_t _runId;                    // 回测运行 ID
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

    // 涉及的标的列表
    Set<symbol_t> _symbols;
    mutable std::mutex _symbolsMtx;
};
