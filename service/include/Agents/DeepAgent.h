#pragma once
#include "Agents/IAgent.h"
#include <tvm/runtime/module.h>

class DeepAgent : public IAgent {
public:
    DeepAgent(const String& path, int classes, const nlohmann::json& params);
    ~DeepAgent();

    virtual int predict(const DataFeatures& data, Vector<float>& result);

    virtual void train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch);
private:
    short _classes;
    String _modelpath;
    nlohmann::json _params;

    tvm::runtime::Module _module;
    tvm::runtime::Module _model;
};