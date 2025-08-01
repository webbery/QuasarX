#pragma once
#include "std_header.h"
#include "Strategy.h"
#include "Transfer.h"
#include "xgboost/c_api.h"
#include "json.hpp"
#include "Agents/IAgent.h"

class Server;
struct AgentStrategyInfo;

class XGBoostAgent : public IAgent {
public:
    XGBoostAgent(const String& path, int classes, const nlohmann::json& params);
    ~XGBoostAgent();

    virtual int classify(const DataFeatures& data, short n_samples, Vector<float>& result);
    virtual double predict();

    virtual void train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch);
private:
    BoosterHandle _booster;
    short _classes;
    String _modelpath;
    nlohmann::json _params;
};

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
        IStrategy* _strategy = nullptr;
        Transfer* _transfer = nullptr;
        char _future = 0;
    };

    Array<Map<String, PipelineInfo>, 2> _pipelines; // 0-virtual, 1- real 
};