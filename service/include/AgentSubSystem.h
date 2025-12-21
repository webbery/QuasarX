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

    void Start();
    void Start(const String& strategy);

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
    RiskSubSystem* _riskSystem = nullptr;

    struct StrategyFlowInfo {
        std::atomic_bool _running = false;
        std::thread* _worker = nullptr;
        nlohmann::json _config;

        Map<StatisticIndicator, std::variant<float, List<float>>> _collections;
        List<QNode*> _graph;
        // 择时模块
        ITimingStrategy* _timing = nullptr;
    };

    Map<String, StrategyFlowInfo> _flows; 
    Set<time_range> _stock_working_range;
};
