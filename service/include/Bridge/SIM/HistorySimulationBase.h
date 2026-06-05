#pragma once
#include "Bridge/exchange.h"
#include "Bridge/SlippageModel.h"
#include "Bridge/SIM/BacktestContext.h"
#include "DataFrame/DataFrame.h"
#include "Util/system.h"
#include <nng/nng.h>
#include <atomic>
#include <shared_mutex>
#include <memory>

using DataFrame = hmdf::StdDataFrame<uint32_t>;

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
    run_id_t createBacktestContext(
        const String& strategy_name,
        const Set<symbol_t>& symbols,
        double initial_capital = 100000.0
    );

    BacktestContext* getBacktestContext(run_id_t run_id);
    const BacktestContext* getBacktestContext(run_id_t run_id) const;
    void destroyBacktestContext(run_id_t run_id);

    bool stepForward(BacktestContext* context);
    QuoteInfo GetQuote(symbol_t symbol, run_id_t run_id);
    order_id AddOrder(const symbol_t& symbol, OrderContext* order, uint32_t strategy_hash);

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

    // === 回测时间范围配置 ===
    void SetBacktestTimeRange(time_t start, time_t end);
    bool HasBacktestTimeRange() const;
    time_t GetBacktestStartTime() const;
    time_t GetBacktestEndTime() const;

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
    bool LoadCSVToDataFrame(const String& file_path, DataFrame& df, Vector<String>& header);
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
    Map<symbol_t, DataFrame> _csvs;       // 复权数据（用于指标计算）
    Map<symbol_t, DataFrame> _org_csvs;   // 原始数据（用于实际买卖）
    Map<symbol_t, Vector<String>> _headers;
    Map<symbol_t, Vector<String>> _org_headers;

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
};
