#pragma once
#include "DataGroup.h"
#include "std_header.h"
#include "Strategy.h"
#include "Transfer.h"
#include "json.hpp"
#include "Agents/IAgent.h"

class Server;
struct AgentStrategyInfo;
class RiskSubSystem;

class AgentSubsystem  {
public:
    AgentSubsystem(Server* handle);
    ~AgentSubsystem();

    bool LoadConfig(const AgentStrategyInfo& config);

    void Start();

    void Train(const String& strategy);

    void Create(const String& strategy, SignalGeneratorType type, const nlohmann::json& params);

    void RegistCollection(const String& strategy, const Set<String>& );

    void ClearCollections(const String& strategy);

    const Map<String, std::variant<float, List<float>>>& GetCollection(const String& strategy) const;
private:
    void RunBacktest(const String& strategyName, QStrategy* strategy, const DataFeatures& input);

    void RunInstant(const String& strategyName, QStrategy* strategy, const DataFeatures& input);

    void ProcessToday(const String& strategy, const DataFeatures& symbol);

    void PredictTomorrow(const String& strategyName, QStrategy* strategy, const DataFeatures& input);

    bool ImmediatelyBuy(const String& strategy, symbol_t symbol, double price, OrderType type);
    bool ImmediatelySell(const String& strategy, symbol_t symbol, double price, OrderType type);

    bool DailyBuy(const String& strategy, symbol_t symbol, const DataFeatures& features);
    bool DailySell(const String& strategy, symbol_t symbol, const DataFeatures& features);

    bool StrategySell(const String& strategyName, symbol_t symbol, const DataFeatures& features);
    bool IsNearClose(symbol_t symb);
    // 生成交易信号
    bool GenerateSignal(symbol_t symbol, const DataFeatures& features);

private:
    Server* _handle;
    RiskSubSystem* _riskSystem = nullptr;

    struct PipelineInfo {
        IAgent* _agent = nullptr;
        // IStrategy* _strategy = nullptr;
        Transfer* _transfer = nullptr;
        QStrategy* _strategy = nullptr;
        char _future = 0;
        Map<String, std::variant<float, List<float>>> _collections;
        Map<String, PrimitiveFeature*> _featureCalculator;
    };

    Map<String, PipelineInfo> _pipelines; 
    Set<time_range> _stock_working_range;
};
