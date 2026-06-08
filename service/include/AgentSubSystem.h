#pragma once
#include "KBarBuilder.h"
#include "MarketTiming.h"
#include "StrategyNode.h"
#include "std_header.h"
#include "Strategy.h"
#include "Util/datetime.h"
#include "json.hpp"
#include <atomic>
#include <memory>

class Server;
class RiskSubSystem;
class ExchangeManager;
enum class StatisticIndicator: char;
enum class OrderType: char;

class FlowSubsystem  {
    struct StrategyFlowInfo;
public:
    FlowSubsystem(Server* handle);
    ~FlowSubsystem();

    bool LoadFlow(const String& strategy, const List<QNode*>& topo_flow);
    void ClearFlow(const String& strategy);

    void Start();
    void Stop(const String& strategy);

    /**
     * @brief 启动回测（多线程版本）
     * @param strategy 策略名称
     * @param symbols 标的列表
     * @param initialCapital 初始资金
     */
    run_id_t Start(const String& strategy, const Set<symbol_t>& symbols, double initialCapital = 100000.0);

    /**
     * @brief 启动已创建上下文的回测工作线程（多 Exchange 协调版本）
     * @param strategy 策略名称
     * @param runId 已分配的回测运行 ID
     * @param exchangeMgr Exchange 协调器
     */
    void StartBacktestWithExchangeMgr(const String& strategy, run_id_t runId, ExchangeManager* exchangeMgr);

    /**
     * @brief 设置策略为影子模式（在 LoadFlow 之前调用）
     * @note 不调用此方法时，策略默认跟随全局模式
     */
    void SetShadowMode(const String& strategy);

    /**
     * @brief 启动实盘策略（K-bar 聚合驱动）
     */
    run_id_t StartRealtime(const String& strategy, const Set<symbol_t>& symbols, double initialCapital = 100000.0);

    /**
     * @brief 从策略图中获取需要的数据源集合
     */
    Set<String> GetRequiredSources(const String& strategy) const;

    /**
     * @brief 检查策略是否正在运行
     */
    bool IsRunning(const String& strategy) const;

    /**
     * @brief 获取所有已载入的策略名称
     */
    List<String> GetFlowNames() const;

    /**
     * @brief 获取策略的 epoch 计数（回测总周期数或实盘执行次数）
     */
    uint64_t GetEpochCount(const String& strategy) const;

    /**
     * @brief 获取策略最后一次成功执行的时间戳（Unix time_t）
     */
    time_t GetLastHeartbeat(const String& strategy) const;

    /**
     * @brief 计算回测指标和 MonteCarlo 模拟，结果写入 flow._collections 和 flow._mcPaths
     * 
     * @param strategy      策略名称（用于日志）
     * @param flow          策略流信息（输出指标会写入 flow._collections 和 flow._mcPaths）
     * @param btContext     回测上下文（提供组合价值快照和资产快照）
     * @param exchangeMgr   Exchange 管理器（用于查询交易模式）
     * 
     * @note 此函数会：
     *       1. 从 btContext 提取组合价值序列
     *       2. 计算传统统计指标（收益、夏普、回撤等）
     *       3. 运行 20,000 次 MonteCarlo 模拟（含压力测试）
     *       4. 多资产时计算协方差诊断
     *       5. 将结果写入 flow._collections 和 flow._mcPaths
     */
    void ComputeBacktestMetrics(
        const String& strategy,
        StrategyFlowInfo& flow,
        BacktestContext* btContext,
        ExchangeManager* exchangeMgr
    );

    // void Create(const String& strategy, SignalGeneratorType type, const nlohmann::json& params);

    void RegistCollection(const String& strategy, const Set<String>& );

    void ClearCollections(const String& strategy);

    const Map<StatisticIndicator, std::variant<float, List<float>>>& GetCollection(const String& strategy) const;
    
    Set<symbol_t> GetPools(const String& strategy);

    /**
     * @brief 获取回测每日收益数据（回测结束后调用）
     */
    struct BacktestDailyReturns {
        Vector<time_t> dates;
        Vector<double> returns;
    };
    BacktestDailyReturns GetBacktestDailyReturns(const String& strategy) const;

    /**
     * @brief 蒙特卡洛模拟路径数据（供前端可视化）
     */
    struct McPathDetail {
        double total_return = 0;
        double max_drawdown = 0;
        double win_rate = 0;
        int longest_win_streak = 0;
        int longest_loss_streak = 0;
        size_t max_dd_bar_index = 0;
        double vol_ratio = 1.0;
        Vector<double> equity_curve;
    };

    struct BacktestMcPaths {
        Vector<McPathDetail> worst_paths;
        Vector<McPathDetail> best_paths;
        McPathDetail median_path;
        McPathDetail p10_path;
        McPathDetail p90_path;
    };

    BacktestMcPaths GetBacktestMcPaths(const String& strategy) const;

private:

    /**
     * @brief 启动回测（内部方法，仅回测模式调用）
     */
    run_id_t StartBacktest(const String& strategy, const Set<symbol_t>& symbols, double initialCapital);

    bool ImmediatelyBuy(const String& strategy, symbol_t symbol, double price, OrderType type);
    bool ImmediatelySell(const String& strategy, symbol_t symbol, double price, OrderType type);

    // bool DailyBuy(const String& strategy, symbol_t symbol, const DataFeatures& features);
    // bool DailySell(const String& strategy, symbol_t symbol, const DataFeatures& features);

    // bool StrategySell(const String& strategyName, symbol_t symbol, const DataFeatures& features);
    bool IsNearClose(symbol_t symb);

    bool RunGraph(const String& strategy, const StrategyFlowInfo& flow, DataContext& context);

    void RegistIndicator(const String& strategy);

    bool IsUseShareMemory(const StrategyFlowInfo& flow);

    /**
     * @brief 设置策略级别的运行模式（在 LoadFlow 之前调用）
     */
    void SetStrategyRunningMode(const String& strategy, RuningType mode);

private:
    Server* _handle;

    struct StrategyFlowInfo {
        std::atomic_bool _running = false;
        std::thread* _worker = nullptr;

        // 策略级别的影子模式标志（默认 false，跟随全局模式）
        bool isShadowMode = false;

        Map<StatisticIndicator, std::variant<float, List<float>>> _collections;
        List<QNode*> _graph;
        // 择时模块
        ITimingStrategy* _timing = nullptr;

        // 关联的回测运行 ID（多线程回测模式）
        uint16_t _backtestRunId = 0;

        // 每日收益率数据（回测结束后从 context 提取，供 BackTestHandler 使用）
        Vector<time_t> _returnDates;
        Vector<double> _dailyReturns;

        // 蒙特卡洛模拟路径数据（供前端可视化）
        BacktestMcPaths _mcPaths;

        // K-bar 聚合器（实盘模式）
        std::shared_ptr<KBarBuilder> _kbarBuilder;

        // 扩展字段：用于 GET /strategy 返回
        uint64_t _epochCount = 0;       // 总执行周期数
        time_t _lastHeartbeat = 0;      // 最后一次成功执行的时间戳
    };

    Map<String, StrategyFlowInfo> _flows; 
    Set<time_range> _stock_working_range;
};
