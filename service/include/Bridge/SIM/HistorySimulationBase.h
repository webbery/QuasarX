#pragma once
#include "Bridge/exchange.h"
#include "Bridge/SlippageModel.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Util/system.h"
#include <nng/nng.h>
#include <atomic>
#include <shared_mutex>
#include <memory>

struct OHLCVData {
    Vector<time_t> _datetime;
    Vector<double> _open;
    Vector<double> _close;
    Vector<double> _high;
    Vector<double> _low;
    Vector<int64_t> _volume;

    size_t size() const { return _datetime.size(); }
    bool empty() const { return _datetime.empty(); }
};

/**
 * @brief 历史数据回测基类
 *
 * 封装回测引擎的共同逻辑：
 * - CSV 数据加载与管理
 * - BacktestContext 生命周期
 * - stepForward 时间推进（含跨日检测）
 * - matchOrders 订单撮合
 * - 价格访问（后复权/原始价）
 *
 * 子类只需实现：
 * - LoadData(code): 加载指定标的的 CSV
 * - GetDefaultCommission(): 返回默认佣金配置
 * - OnDataLoaded(): 数据加载完成后的初始化（可选）
 */
class HistorySimulationBase : public ExchangeInterface {
public:
    HistorySimulationBase(Server* server);
    virtual ~HistorySimulationBase();

    // === ExchangeInterface 接口（共同实现）===
    virtual const char* Name() override = 0;
    virtual bool Init(const ExchangeInfo& handle) override;
    virtual bool Release() override;
    virtual void SetFilter(const QuoteFilter& filter) override;
    virtual bool Login(AccountType t) override;
    virtual bool IsLogin() override;
    virtual void Logout(AccountType t) override;
    virtual bool GetSymbolExchanges(List<Pair<String, ExchangeName>>& info) override;

    virtual order_id AddOrder(run_id_t run_id, const symbol_t& symbol, OrderContext* order) override;
    virtual void OnOrderReport(order_id id, const TradeReport& report) override;
    virtual Boolean CancelOrder(order_id id, OrderContext* order) override;
    virtual bool GetOrders(SecurityType type, OrderList& ol) override;
    virtual bool GetOrder(const String& sysID, Order& ol) override;
    virtual void QueryQuotes() override;
    virtual void StopQuery() {}
    virtual QuoteInfo GetQuote(symbol_t) override;
    virtual double GetAvailableFunds(run_id_t run_id) override;
    virtual bool GetCommission(symbol_t symbol, List<Commission>& comms) override;
    virtual Boolean HasPermission(symbol_t symbol) override;
    virtual void Reset() override;
    virtual void GetFee(FeeInfo& fee, symbol_t symbol = {}) override {}
    virtual int GetStockLimitation(char type) override;
    virtual bool SetStockLimitation(char type, int limitation) override;
    virtual bool GetPosition(AccountPosition&) override;
    virtual AccountAsset GetAsset() override;

    // === 多线程回测接口（共同实现）===
    // forceReload=true 时无视缓存强制从 DuckDB 重载：日终回放必须看到最新写入的 bar。
    // 缓存 _csvs 只在 Login() 时填充，不强制重载会让新 bar 永远进不了回放窗口。
    run_id_t createBacktestContext(
        const String& strategy_name,
        const Set<symbol_t>& symbols,
        double initial_capital = 100000.0,
        const String& load_start_date = String(),
        const String& load_end_date = String(),
        bool forceReload = false
    );

    BacktestContext* getBacktestContext(run_id_t run_id);
    const BacktestContext* getBacktestContext(run_id_t run_id) const;
    void destroyBacktestContext(run_id_t run_id);

    bool stepForward(BacktestContext* context);
    QuoteInfo GetQuote(symbol_t symbol, run_id_t run_id);

    double GetPrimitivePrice(symbol_t symbol, uint32_t index) const;
    double GetAdjPrice(symbol_t symbol, uint32_t index) const;
    int64_t GetPositionQuantity(symbol_t symbol) const;

    // === 合约信息查询 ===
    virtual bool GetAllStockSymbols(List<SymbolInfo>& symbols) override;
    virtual bool GetAllFundSymbols(List<SymbolInfo>& symbols) override;
    virtual bool GetAllETFSymbols(List<SymbolInfo>& symbols);
    virtual bool GetAllOptionSymbols(List<SymbolInfo>& symbols) override;
    virtual SymbolInfo GetSymbolInfo(const String& code) override;
    virtual void RefreshSymbolList() override;

    double Progress(const String& strategy);
    bool HasBacktestContext(const String& strategy) const;

    // === 回测时间范围配置 ===
    void SetBacktestTimeRange(time_t start, time_t end);
    // 清除时间范围配置（每次回测入口显式调用，避免上一次回测的范围泄漏到本次）
    void ClearBacktestTimeRange();
    bool HasBacktestTimeRange() const;
    time_t GetBacktestStartTime() const;
    time_t GetBacktestEndTime() const;

    // === 数据加载日期范围（LoadData 从 DB 查询时使用）===
    void SetLoadDateRange(const String& startDate, const String& endDate);

    // === 滑点 ===
    void SetSlippageModel(std::unique_ptr<ISlippageModel> model) { _slippageModel = std::move(model); }

    // === 佣金配置 ===
    void SetCommission(const Commission& buy, const Commission& sell);

    // === 交易模式（子类可覆盖）===
    virtual TradingMode GetTradingMode() const { return TradingMode::T1; }

    // === 后复权数据访问（子类可覆盖）===
    virtual std::pair<std::vector<time_t>, std::vector<double>> GetHFQCloseData(symbol_t symbol) const {
        return {};
    }

protected:
    // === 子类必须实现的纯虚函数 ===
    /// @brief 加载指定标的数据到 _csvs / _org_csvs
    virtual bool LoadData(const String& code) = 0;

    /// @brief 返回默认佣金配置（股票 vs ETF 不同）
    virtual std::pair<Commission, Commission> GetDefaultCommission() const = 0;

    /// @brief 数据加载完成后的初始化（如设置 TradingMode）
    virtual void OnDataLoaded() {}

    // === 共同工具方法 ===
    /// 构建 OHLCV 序列。
    ///
    /// 同一时间戳的重复行会被合并（保留最后一次的值），不会产生重复 bar。
    /// 重复 bar 不只是"多一根"：stepForward 只推进当前时间等于 minTime 的标的，
    /// 重复日期会让该标的在原地多走 N 步、其余标的被冻结在旧价上，于是价差/β
    /// 是用不同交易日拼出来的（实测 2024-01-10 出现 11 根，β 从 3.59 滑到 1.82）。
    void BuildOHLCVDataFromMap(
        const Map<String, Vector<double>>& data,
        const Vector<String>& dates,
        OHLCVData& out);

    /// 校验复权序列与原始序列逐 bar 对齐（长度 + 每个时间戳）。
    ///
    /// stepForward 用同一个 curIndex 去取 _csvs 与 _org_csvs，一旦两者行数不同
    /// 就会静默用错位的价格撮合。返回 false 表示数据不可用，调用方应放弃该标的
    /// 而不是继续按 index 硬对齐。
    bool ValidateSeriesAlignment(symbol_t symbol) const;

    /// @brief 把指定标的从行情缓存里删掉
    ///
    /// Login 重新加载前先删：LoadData 失败时不能留下旧条目冒充新数据。
    void DropCachedSymbols(const Set<String>& codes);

    /// @brief 回收无人使用的行情缓存条目，控制共享缓存体积
    ///
    /// 缓存不能整表清空（会连带抹掉并发跑的另一个策略的标的），
    /// 所以靠"保留当前 filter + 活跃回测上下文在用的标的、其余淘汰"来控制增长。
    void EvictUnusedSymbols();

    /// @brief 按当前 filter 逐个加载标的到 _csvs / _org_csvs
    void LoadFilteredSymbols();

    void matchOrders(BacktestContext* context, symbol_t symbol);
    bool OrderReport(BacktestContext* context, order_id id, const TradeReport& report);
    TradeReport OrderMatch(const Order& order, const QuoteInfo& quote);
    void Clear();

    // === 共同成员 ===
    String _org_path;
    nng_socket _sock;
    std::atomic<bool> _finish{false};
    std::atomic<bool> _dataLoadSuccess{false};

    mutable std::shared_mutex _dataMutex;
    Map<symbol_t, OHLCVData> _csvs;       // 复权数据（用于指标计算）
    Map<symbol_t, OHLCVData> _org_csvs;   // 原始数据（用于实际买卖）

    // ============ 线程隔离数据 ============
    ConcurrentMap<uint16_t, std::unique_ptr<BacktestContext>> _backtestContexts;
    std::atomic<uint16_t> _nextRunId{1};

    // ============ 全局状态 ============
    std::atomic<size_t> _cur_id;
    ConcurrentMap<size_t, OrderContext*> _reports;
    Commission _buy;
    Commission _sell;
    std::unique_ptr<ISlippageModel> _slippageModel;

    // 回测时间范围配置（可选）
    bool _hasBacktestTimeRange = false;
    time_t _backtestStartTime = 0;
    time_t _backtestEndTime = 0;

    // 数据加载日期范围（LoadData 查询 DB 时使用，Login 后自动清除）
    String _loadStartDate;
    String _loadEndDate;
};
