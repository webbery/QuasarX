#pragma once
#include "Agents/IAgent.h"
#include "xgboost/c_api.h"

class XGBoostAgent : public IAgent {
public:
    XGBoostAgent(const String& path, int classes, const nlohmann::json& params);
    ~XGBoostAgent();

    virtual int predict(const DataFeatures& data, Vector<float>& result);

    virtual void train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch);
private:
    BoosterHandle _booster;
    short _classes;
    String _modelpath;
    nlohmann::json _params;
};
