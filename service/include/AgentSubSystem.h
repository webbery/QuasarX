#pragma once
#include "MarketTiming.h"
#include "StrategyNode.h"
#include "std_header.h"
#include "Strategy.h"
#include "Util/datetime.h"
#include "json.hpp"
#include <atomic>

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

    // 设置策略配置（用于回测）
    void SetStrategyConfig(const String& strategy, const nlohmann::json& config);

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
     * @brief 检查策略是否正在运行
     */
    bool IsRunning(const String& strategy) const;

    // void Create(const String& strategy, SignalGeneratorType type, const nlohmann::json& params);

    void RegistCollection(const String& strategy, const Set<String>& );

    void ClearCollections(const String& strategy);

    const Map<StatisticIndicator, std::variant<float, List<float>>>& GetCollection(const String& strategy) const;
    
    Set<symbol_t> GetPools(const String& strategy);
private:
    // void RunBacktest(const String& strategyName, QStrategy* strategy, const DataFeatures& input);

    // void RunInstant(const String& strategyName, QStrategy* strategy, const DataFeatures& input);

    // void ProcessToday(const String& strategy, const DataFeatures& symbol);

    // void PredictTomorrow(const String& strategyName, QStrategy* strategy, const DataFeatures& input);

    bool ImmediatelyBuy(const String& strategy, symbol_t symbol, double price, OrderType type);
    bool ImmediatelySell(const String& strategy, symbol_t symbol, double price, OrderType type);

    // bool DailyBuy(const String& strategy, symbol_t symbol, const DataFeatures& features);
    // bool DailySell(const String& strategy, symbol_t symbol, const DataFeatures& features);

    // bool StrategySell(const String& strategyName, symbol_t symbol, const DataFeatures& features);
    bool IsNearClose(symbol_t symb);
    // 生成交易信号
    // bool GenerateSignal(symbol_t symbol, const DataFeatures& features);

    bool RunGraph(const String& strategy, const StrategyFlowInfo& flow, DataContext& context);

    void RegistIndicator(const String& strategy);

    bool IsUseShareMemory(const StrategyFlowInfo& flow);
private:
    Server* _handle;

    struct StrategyFlowInfo {
        std::atomic_bool _running = false;
        std::thread* _worker = nullptr;
        nlohmann::json _config;

        Map<StatisticIndicator, std::variant<float, List<float>>> _collections;
        List<QNode*> _graph;
        // 择时模块
        ITimingStrategy* _timing = nullptr;

        // 关联的回测运行 ID（多线程回测模式）
        uint16_t _backtestRunId = 0;
    };

    Map<String, StrategyFlowInfo> _flows; 
    Set<time_range> _stock_working_range;
};
