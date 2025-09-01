#pragma once
#include "Agents/IAgent.h"
#include "json.hpp"
#ifdef __USE_CUDA__
#include <cuda_runtime.h>
#include <memory>
#ifdef __linux
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <NvInfer.h>
#include <NvOnnxParser.h>
#ifdef __linux
#pragma GCC diagnostic pop
#endif

class CuNNAgent : public IAgent {
public:
    CuNNAgent(const String& path, int classes, const nlohmann::json& params);
    ~CuNNAgent();

    virtual int predict(const DataFeatures& data, Vector<float>& result);

    virtual void train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch);

private:
    bool createEngineFromONNX(const std::string& onnxModelPath);

    bool saveEngine(const String& enginePath);
    bool loadEngine(const String& enginePath);
    
    bool prepareInputSpace();

private:
    short _classes;
    String _modelpath;
    nlohmann::json _params;

    nvinfer1::IBuilder* _builder;
    nvinfer1::ICudaEngine* _engine;
    std::unique_ptr<nvinfer1::IExecutionContext> _context;
    std::unique_ptr<nvinfer1::IRuntime> _runtime;

    nvinfer1::Dims _inputDims;
    nvinfer1::Dims _outputDims;

    void* _gpuInput;
    void* _gpuOutput;
};

using DeepAgent = CuNNAgent;
#else
#include <onnxruntime/onnxruntime_cxx_api.h>

class OnnxNNAgent : public IAgent {
    friend class NerualNetworkAgentManager;
public:
    ~OnnxNNAgent();

    virtual int predict(const DataFeatures& data, Vector<float>& result);

    virtual void train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch);
private:
    OnnxNNAgent(const String& path, const Ort::Env& env, const Ort::SessionOptions& opt, int classes, const nlohmann::json& params);

private:
    Ort::Session _session;
};

using DeepAgent = OnnxNNAgent;
#endif

class NerualNetworkAgentManager {
public:
    static NerualNetworkAgentManager& GetInstance();

    IAgent* GenerateAgent(const String& path, const nlohmann::json& params);

private:
    NerualNetworkAgentManager();

private:
    Ort::Env _env;
    Ort::SessionOptions _sessionOptions;
};