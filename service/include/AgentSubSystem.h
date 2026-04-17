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
     * @brief 启动实盘策略（K-bar 聚合驱动）
     */
    run_id_t StartRealtime(const String& strategy, const Set<symbol_t>& symbols, double initialCapital = 100000.0);

    /**
     * @brief 检查策略是否正在运行
     */
    bool IsRunning(const String& strategy) const;

    // void Create(const String& strategy, SignalGeneratorType type, const nlohmann::json& params);

    void RegistCollection(const String& strategy, const Set<String>& );

    void ClearCollections(const String& strategy);

    const Map<StatisticIndicator, std::variant<float, List<float>>>& GetCollection(const String& strategy) const;
    
    Set<symbol_t> GetPools(const String& strategy);

    /**
     * @brief 获取策略的预热期 epoch 数
     */
    int GetWarmupEpochs(const String& strategy) const {
        auto it = _flows.find(strategy);
        return it != _flows.end() ? it->second._warmupEpochs : 0;
    }

    /**
     * @brief 设置策略的预热期 epoch 数（由 StrategySubSystem 调用）
     */
    void SetWarmupEpochs(const String& strategy, int epochs) {
        _flows[strategy]._warmupEpochs = epochs;
    }

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

private:
    Server* _handle;

    struct StrategyFlowInfo {
        std::atomic_bool _running = false;
        std::thread* _worker = nullptr;

        Map<StatisticIndicator, std::variant<float, List<float>>> _collections;
        List<QNode*> _graph;
        // 择时模块
        ITimingStrategy* _timing = nullptr;

        // 关联的回测运行 ID（多线程回测模式）
        uint16_t _backtestRunId = 0;

        // 预热期配置（回测模式下跳过信号生成的 epoch 数）
        int _warmupEpochs = 0;

        // K-bar 聚合器（实盘模式）
        std::shared_ptr<KBarBuilder> _kbarBuilder;
    };

    Map<String, StrategyFlowInfo> _flows; 
    Set<time_range> _stock_working_range;
};
