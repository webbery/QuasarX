#pragma once
#include "std_header.h"
#include "Bridge/exchange.h"
#include "Bridge/SIM/StockPositionManager.h"
#include "Bridge/CapitalPool.h"
#include "Metric/CUSUMDetector.h"
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

    // 持仓跟踪（委托给 StockPositionManager）
    int64_t getPosition(symbol_t symbol) const;
    void setPosition(symbol_t symbol, int64_t qty);
    
    /// @brief 调整持仓（通用：delta > 0 买入，delta < 0 卖出）
    /// 成交时自动从 CapitalPool 扣/加资金
    void adjustPosition(symbol_t symbol, int delta, double price);

    // 资金管理（委托给 CapitalPool）
    void setCapitalPool(CapitalPool* pool) { _capitalPool = pool; }
    void setStrategyName(const String& name) { _strategyNameForCapital = name; }
    double getCapital() const;
    double getAvailableFunds() const;
    void setCapital(double capital);  // 初始化策略资金

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

    // 跨日检测（T+0/T+1 控制用）
    time_t getLastTradeDay() const { return _lastTradeDay; }
    void setLastTradeDay(time_t day) { _lastTradeDay = day; }
    void onDayChange() { _positionMgr.OnDayChange(); }

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

    // === 多资产每日快照（按标的独立记录持仓市值）===
    void reserveDailyAssetSnapshots(size_t n) {
        _assetValues.reserve(n);
    }
    void recordDailyAssetSnapshot(time_t date, const Map<symbol_t, double>& asset_values) noexcept {
        _assetSnapshotDates.emplace_back(date);
        _assetValues.emplace_back(asset_values);
    }
    size_t assetSnapshotCount() const noexcept { return _assetSnapshotDates.size(); }
    const Vector<time_t>& getAssetSnapshotDates() const noexcept { return _assetSnapshotDates; }
    const Vector<Map<symbol_t, double>>& getAssetValues() const noexcept { return _assetValues; }

    // === 摩擦成本累计 ===
    void addFrictionCost(double cost) noexcept { _totalFrictionCost += cost; }
    double getTotalFrictionCost() const noexcept { return _totalFrictionCost; }

    // === CUSUM 变点检测 ===
    /// @brief 推送当日收益率到 CUSUM 检测器
    /// @param daily_return 当日组合收益率
    /// @return 是否检测到变点
    /// @note 前 min_obs 个数据用于校准 mu/sigma，之后用实际统计量更新 detector 配置
    bool updateCUSUM(double daily_return) noexcept {
        _cusum_returns.push_back(daily_return);

        // 收集足够数据后自适应校准：用实际收益率的 mu/sigma 替换默认值
        if (!_cusum_calibrated &&
            _cusum_returns.size() >= _cusum_detector.get_config()._min_obs) {
            double mu = 0.0, sigma = 0.0;
            for (double r : _cusum_returns) mu += r;
            mu /= _cusum_returns.size();
            for (double r : _cusum_returns) {
                double d = r - mu;
                sigma += d * d;
            }
            sigma = std::sqrt(sigma / _cusum_returns.size());
            if (sigma < 1e-10) sigma = 1e-10;  // 防止除零

            // 用实际统计量重置 detector 并重喂历史数据
            CUSUMConfig cfg = _cusum_detector.get_config();
            cfg._mu = mu;
            cfg._sigma = sigma;
            _cusum_detector.set_config(cfg);  // set_config 内部会 reset()
            for (double r : _cusum_returns) {
                _cusum_detector.update(r);
            }
            _cusum_calibrated = true;
        }

        // 校准前用默认参数 update，校准后用实际参数 update
        if (_cusum_calibrated) {
            return _cusum_detector.update(daily_return)._change_point;
        }
        return _cusum_detector.update(daily_return)._change_point;
    }

    const CUSUMDetector& getCUSUMDetector() const noexcept { return _cusum_detector; }

private:
    run_id_t _runId;                    // 回测运行 ID
    String _strategy_name;              // 策略名称

    // 每个标的独立的时间游标
    Map<symbol_t, std::atomic<uint32_t>*> _curIndices;
    mutable std::mutex _indexMtx;

    // 持仓状态（由 StockPositionManager 统一管理）
    StockPositionManager _positionMgr;

    // 资金管理（委托给 CapitalPool，不拥有）
    CapitalPool* _capitalPool = nullptr;
    String _strategyNameForCapital;  // 用于 CapitalPool 查询的策略名
    double _initialCapital = 0.0;    // 策略初始资金（从 CapitalPool 分配）

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

    // 上一个交易日（用于跨日检测）
    time_t _lastTradeDay = 0;

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

    // 多资产每日快照（每个标的独立的持仓市值）
    Vector<time_t> _assetSnapshotDates;
    Vector<Map<symbol_t, double>> _assetValues;

    // 摩擦成本累计（佣金 + 印花税 + 滑点）
    double _totalFrictionCost = 0.0;

    // === CUSUM 变点检测 ===
    CUSUMDetector _cusum_detector;
    Vector<double> _cusum_returns;      // 用于自适应校准的返回值缓存
    bool _cusum_calibrated = false;     // 是否已完成 mu/sigma 校准
};

