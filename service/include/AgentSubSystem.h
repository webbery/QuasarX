#pragma once
#include "std_header.h"
#include "Strategy.h"
#include "Transfer.h"
#include "json.hpp"
#include "Agents/IAgent.h"

class Server;
struct AgentStrategyInfo;

class AgentSubsystem  {
public:
    AgentSubsystem(Server* handle);
    ~AgentSubsystem();

    bool LoadConfig(const AgentStrategyInfo& config);

    void Start();

    void Train(const String& strategy);

    void Create(const String& strategy, AgentType type, const nlohmann::json& params);
private:

private:
    Server* _handle;

    struct PipelineInfo {
        IAgent* _agent = nullptr;
        // IStrategy* _strategy = nullptr;
        Transfer* _transfer = nullptr;
        QStrategy* _strategy = nullptr;
        char _future = 0;
    };

    Map<String, PipelineInfo> _pipelines; 
};