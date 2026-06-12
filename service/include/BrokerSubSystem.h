#pragma once
#include "std_header.h"
#include "Bridge/exchange.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Util/lmdb.h"
#include "json.hpp"
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <condition_variable>
#include "PortfolioSubsystem.h"
#include <boost/lockfree/queue.hpp>

// class Broker {
// public:
//   virtual ~Broker() {}

//   virtual int Buy(const String& strategy, symbol_t, const Order& order, TradeInfo& detail) = 0;

//   virtual int Sell(const String& strategy, symbol_t, const Order& order, TradeInfo& detail) = 0;

//   virtual double Put(symbol_t, const Order& order)  {return 0; }

//   virtual double Call(symbol_t, const Order& order) { return 0; }

//   // 行权
//   virtual int Exercise(symbol_t, const Order& order, TradeInfo& info) { return 0; }

//   virtual uint32_t Statistic(float confidence, int N, std::shared_ptr<DataGroup> group, nlohmann::json& indexes) = 0;

//   // 异步下单
//   virtual int64_t AddOrder(symbol_t, const Order& order, std::function<void(const TradeReport&)> cb) = 0;
//   // 查询订单
//   virtual bool QueryOrders(OrderList& ol) = 0;
//   virtual int QueryOrder(uint32_t orderID, Order& order) = 0;
//   virtual void CancelOrder(order_id id) = 0;
// };

enum class StatisticIndicator: char {
  VaR,
  ES,
  MaxDrawDown,
  AnualReturn,
  AnualSharp,
  Sharp,
  Calmar,
  Infomation,
  WinRate,
  NumTrades,
  Extreme,
  TotalReturn,
  AnnualVol,    // 年化波动率

  // Bootstrap 蒙特卡洛风险分析指标
  BootRuinProb50,       // 爆仓概率 (<50%)
  BootRuinProb30,       // 爆仓概率 (<30%)
  BootReturnP5,         // 收益率 5% 分位数
  BootReturnP50,        // 收益率中位数
  BootReturnP95,        // 收益率 95% 分位数
  BootMaxDDP50,         // 最大回撤中位数
  BootMaxDDP95,         // 最大回撤 95% 分位数
  BootMedianAnnualRet,  // 中位数年化收益
  BootTail1PctAvgDD,    // 最差 1% 平均回撤
  BootMethod,           // 0=Standard, 1=Block
  BootBlockSize,        // Block 大小
  BootAutocorrelation,  // 自相关系数
  BootNSimulations,     // 模拟次数
  // 压力测试（基础：波动率 × N）
  BootStressRuinProb50,
  BootStressRuinProb30,
  BootStressReturnP5,
  BootStressReturnP50,
  BootStressMaxDDP50,
  // 流动性压力测试（尾部收益率折扣）
  BootLiqStressRuinProb50,
  BootLiqStressReturnP5,
  BootLiqStressMaxDDP50,
  // 波动率聚集压力测试（更大 Block Size）
  BootVolClusterStressRuinProb50,
  BootVolClusterStressReturnP5,
  BootVolClusterStressMaxDDP50,
  R2,  // 样本外拟合能力 (组合价值对时间线性回归的 R²)
  // 协方差诊断
  CovConditionNumber,    // 条件数
  CovMinCorr,            // 最小相关系数
  CovMaxCorr,            // 最大相关系数
  CovPositiveDefinite,   // 是否正定 (0/1)
  CovObservations,       // 有效观测数
  CovNAzets,            // 资产数
  CovNearCollinear,      // |ρ| > 0.95 配对数
  DragCostToReturn,      // 拖累成本/收益比 = 总摩擦成本 / 总收益绝对值
};

class ICommission {
public:
  virtual double GetCommission(symbol_t symbol, int64_t size) = 0;
};

class StockCommission : public ICommission {
public:
  StockCommission();

  virtual double GetCommission(symbol_t symbol, int64_t size);

public:
  double _min = 5;
  double _fee = 0.00001345;
};

class FutureCommission : public ICommission {
public:
  virtual double GetCommission(symbol_t symbol, int64_t size) { return 0; }
};

class OptionCommission: public ICommission {
public:
  virtual double GetCommission(symbol_t symbol, int64_t size) { return 0; }
};

class IndexCommission: public ICommission {
public:
  virtual double GetCommission(symbol_t symbol, int64_t size) { return 0; }
};

using SymbolTrades = Map<symbol_t, List<Transaction>>;

// 每个 run_id 的私有数据，带锁保护
struct RunIdData {
    mutable std::mutex mtx;
    SymbolTrades trades;
};

class BrokerSubSystem {
public:
    using predictions_t = List<Pair<fixed_time_range, int>>;

    // 开启模拟则全部使用模拟
    BrokerSubSystem(Server* server, bool is_simulation);

    virtual ~BrokerSubSystem() { Release(); }

    bool Init(const nlohmann::json& config, const Map<ExchangeType, ExchangeInterface*>& brokers);

    void Release();

    [[deprecated("使用异步版本 Buy(strategy, symbol, order, callback)")]]
    order_id Buy(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& detail);

    [[deprecated("使用异步版本 Sell(strategy, symbol, order, callback)")]]
    order_id Sell(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& detail);

    order_id Buy(run_id_t run_id, const String& strategy, symbol_t symbol, const Order& order, std::function<void (const TradeReport&)> cb);

    order_id Sell(run_id_t run_id, const String& strategy, symbol_t symbol, const Order& order, std::function<void (const TradeReport&)> cb);
    order_id Exercise(run_id_t run_id, const String& strategy, symbol_t symbol, const Order& order, std::function<void (const TradeReport&)> cb);

    int64_t AddOrder(run_id_t run_id, symbol_t, const Order& order, std::function<void(const TradeReport&)> cb);

    virtual bool QueryOrders(SecurityType type, OrderList& ol);
    virtual int QueryOrder(const String& sysID, Order& order);
    virtual void CancelOrder(order_id& id, symbol_t symbol, std::function<void (const TradeReport&)> cb);
    // 记录交易（从 OrderContext 中获取 backtest_run_id）
    void RecordTrade(const OrderContext& context);
    // 清除历史交易信息
    void CleanStrategyRecord();
    // 清除指定 run_id 的历史交易记录（防止多次回测累积）
    void ClearHistoryTrades(run_id_t run_id);
    // 持久化指定 run_id 的交易记录
    void PersistTrades(run_id_t run_id);

    // 注册统计指标
    void RegistIndicator(const String& strategy, StatisticIndicator indicator);
    void UnRegistIndicator(const String& strategy, StatisticIndicator indicator);
    void CleanAllIndicators(const String& strategy);
    const Set<StatisticIndicator> GetIndicatorsName(const String& strategy) const { return _indicators.at(strategy); }
    float GetIndicator(const String& name, StatisticIndicator indicator);
    StringView GetIndicatorName(StatisticIndicator indicator);

    double GetProfitLoss();

    void SetCommission(symbol_t symbol, const Commission& comm);
    // 设置滑点
    void SetSlip(float val) { _slip = val; }
    // 
    void PredictWithDays(symbol_t symb, int N, int op);
    bool GetNextPrediction(symbol_t symb, fixed_time_range& tr, int& op);
    void DoneForecast(symbol_t symb, int operation);

    const predictions_t& QueryPredictionOfHistory(symbol_t symb);
    const Map<symbol_t, predictions_t>& QueryPredictionOfHistory() { return _predictions; }

    void DeletePrediction(symbol_t, int index);

    List<Transaction> GetHistoryTrades(run_id_t run_id, symbol_t);

    // 获取所有交易记录（用于持久化）
    std::shared_ptr<RunIdData> GetAllHistoryTrades(run_id_t run_id);

    // 交易查询结构
    struct TradeQueryResult {
        List<Transaction> trades;
        size_t totalCount = 0;
    };

    // 通用交易查询接口
    TradeQueryResult QueryTrades(symbol_t symbol,
                                const String& strategy = "",
                                time_t start = 0,
                                time_t end = 0,
                                size_t offset = 0,
                                size_t limit = 0);

    // 净值查询结果
    struct NavResult {
        Vector<time_t> dates;
        Vector<double> values;
    };

    // 根据交易记录动态计算净值曲线
    NavResult QueryNav(run_id_t runId, time_t start, time_t end);

    Set<symbol_t> GetPoolSymbols(const String& name);

    void ProcessOrderSuccess(const String& strategy, symbol_t symbol, const TradeReport& report);

private:
    order_id AddOrderBySide(run_id_t run_id, const String& strategy, symbol_t symbol, const Order& order, TradeInfo& detail, int side);
    order_id AddOrderBySide(run_id_t run_id, const String& strategy, symbol_t symbol, const Order& order, int side, std::function<void (const TradeReport&)> cb);

    order_id AddOrderAsync(run_id_t run_id, OrderContext* order);

private:
    void run();
    
    void flush(MDB_txn* txn, MDB_dbi dbi);

    double VaR(float confidence);
    double ES(double var);

    MDB_dbi GetDBI(int portfolid_id, MDB_txn* txn);

    void InitPortfolio(MDB_txn* txn, MDB_dbi);
    void InitHistory(MDB_txn* txn, MDB_dbi);
    void InitPrediction(MDB_txn* txn, MDB_dbi);

    nlohmann::json GetHistoryJson();
    nlohmann::json GetPortfolioJson();
    nlohmann::json GetBrokers();
    nlohmann::json GetPrediction();

    nlohmann::json LoadJson(const String& name, MDB_txn* txn, MDB_dbi);
    void SaveJson(const String& name, MDB_txn* txn, MDB_dbi, const nlohmann::json& jsn);

    ICommission* GetCommision(symbol_t symbol);

private:
    Server* _server;
    PortfolioSubSystem* _portfolio;
    bool _simulation;
    // 交易记录 - 双层索引：run_id -> symbol -> trades
    ConcurrentMap<run_id_t, std::shared_ptr<RunIdData>> _historyTrades;

    std::mutex _indMtx;
    Map<String, Set<StatisticIndicator>> _indicators;

    std::shared_mutex _predMtx;
    Map<symbol_t, predictions_t> _predictions;
    Map<symbol_t, Pair<fixed_time_range, int>> _symbolOperation;

    Map<int, MDB_dbi> _dbis;
    // 交易手续费
    Commission _stockCommission;
    Map<symbol_t, Commission> _future;
    Map<symbol_t, ICommission*> _commissions;

    bool _update : 1;
    bool _exit : 1;
    float _slip;
    String _dbpath;
    std::thread* _thread;
    std::mutex _mutex;
    std::condition_variable _cv;

    // 订单队列
    boost::lockfree::queue<OrderContext*> _order_queue;
    // 
    static Map<ExchangeType, ExchangeInterface*> _exchanges;

};