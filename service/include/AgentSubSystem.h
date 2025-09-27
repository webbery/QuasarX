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

    void Create(const String& strategy, AgentType type, const nlohmann::json& params);

    void RegistCollection(const String& strategy, const Set<String>& );

    void ClearCollections(const String& strategy);

    const Map<String, std::variant<float, List<float>>>& GetCollection(const String& strategy) const;
private:
    void RunBacktest(QStrategy* strategy, const DataFeatures& input);

    void RunInstant(QStrategy* strategy, const DataFeatures& input);

    void ProcessToday(const DataFeatures& symbol);

    void PredictTomorrow(QStrategy* strategy, const DataFeatures& input);

    bool ImmediatelyBuy(symbol_t symbol, double price, OrderType type);
    bool ImmediatelySell(symbol_t symbol, double price, OrderType type);

    bool DailyBuy(symbol_t symbol, const DataFeatures& features);
    bool DailySell(symbol_t symbol, const DataFeatures& features);

    bool StrategySell(symbol_t symbol, const DataFeatures& features);
    bool IsNearClose(symbol_t symb);

    void UpdateCollection(const String& name, const DataFeatures& feature);
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
    };

    Map<String, PipelineInfo> _pipelines; 
    Set<time_range> _stock_working_range;
};