#pragma once
#include "std_header.h"
#include "Transfer.h"
#include "xgboost/c_api.h"
#include "json.hpp"
#include "Agents/IAgent.h"

class Server;
struct AgentStrategyInfo;

class XGBoostAgent : public IAgent {
public:
    XGBoostAgent(const String& path, const nlohmann::json& params);
    ~XGBoostAgent();

    virtual int classify(const Vector<float>& data, short n_samples, short n_features, Vector<float>& result);
    virtual double predict();

    virtual void train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch);
private:
    BoosterHandle* _booster = nullptr;
    String _modelpath;
    nlohmann::json _params;
};

class AgentSubsystem  {
public:
    AgentSubsystem(Server* handle);
    ~AgentSubsystem();

    void LoadConfig(const AgentStrategyInfo& config);

    void Start();

    void Train(const String& strategy);

    void Create(const String& strategy, AgentType type, const nlohmann::json& params);
private:

private:
    Server* _handle;

    struct PipelineInfo {
        IAgent* _agent = nullptr;
        Transfer* _transfer = nullptr;
    };

    Map<String, PipelineInfo> _pipelines;
};