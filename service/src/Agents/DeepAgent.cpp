#include "Agents/DeepAgent.h"
#include <cstring>
#include <fstream>
#include <memory>

#ifdef __USE_CUDA__
using namespace nvinfer1;
using namespace nvonnxparser;

class TensorLogger : public ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            INFO("TensorRT: {}", msg);
        }
    }
};

namespace {
    TensorLogger logger;

    auto StreamDeleter = [](cudaStream_t* pStream) {
        if (pStream)
        {
            static_cast<void>(cudaStreamDestroy(*pStream));
            delete pStream;
        }
    };
}

CuNNAgent::CuNNAgent(const String& path, int classes, const nlohmann::json& params)
:_modelpath(path), _params(params), _classes(classes)
, _builder(nullptr), _engine(nullptr), _context(nullptr)
, _gpuInput(nullptr), _gpuOutput(nullptr) {
    _builder = nvinfer1::createInferBuilder(logger);
    if (!_builder) {
        FATAL("DeepAgent Builder create fail.");
        return;
    }
    if (!createEngineFromONNX(_modelpath)) {
        FATAL("DeepAgent Import ONNX fail.");
        return;
    }

    _context.reset(_engine->createExecutionContext());
    if (!_context) {
        FATAL("DeepAgent create context fail.");
        return;
    }
}

CuNNAgent::~CuNNAgent() {
}

int CuNNAgent::predict(const DataFeatures& data, Vector<float>& result) {
    assert(data._dimesion.size() > 0);

    std::unique_ptr<IExecutionContext> context(_engine->createExecutionContext());
    if (!context)
    {
        return -1;
    }
    return 0;
}

void CuNNAgent::train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch) {

}

bool CuNNAgent::createEngineFromONNX(const std::string& onnxModelPath)
{
    const auto explicitBatch = 1U << static_cast<uint32_t>(NetworkDefinitionCreationFlag::kEXPLICIT_BATCH);
    std::unique_ptr<INetworkDefinition> network(_builder->createNetworkV2(explicitBatch));

    std::unique_ptr<IParser> parser(createParser(*network, logger));
    if (parser->parseFromFile(onnxModelPath.c_str(), (int)ILogger::Severity::kWARNING)) {
        FATAL("parse file {} error.", onnxModelPath);
        for (int32_t i = 0; i < parser->getNbErrors(); ++i)
        {
            FATAL("{}", parser->getError(i)->desc());
        }
        return false;
    }
    std::unique_ptr<IBuilderConfig> config(_builder->createBuilderConfig());
    config->setMemoryPoolLimit(MemoryPoolType::kWORKSPACE, 1 << 24); // 16MB

    std::unique_ptr<cudaStream_t, decltype(StreamDeleter)> pStream(new cudaStream_t, StreamDeleter);
    if (cudaStreamCreateWithFlags(pStream.get(), cudaStreamNonBlocking) != cudaSuccess) {
        pStream.reset(nullptr);
        FATAL("create cuda stream fail.");
        return false;
    }
    config->setProfileStream(*pStream);

    _engine = _builder->buildEngineWithConfig(*network, *config);

    //saveEngine();
    // 计算输入和输出的大小
    assert(network->getNbInputs() == 1);
    _inputDims = network->getInput(0)->getDimensions();
    assert(network->getNbOutputs() == 1);
    _outputDims = network->getOutput(0)->getDimensions();
    return true;
}

bool CuNNAgent::saveEngine(const String& enginePath)
{
    std::ofstream file(enginePath, std::ios::binary);
    if (!file) {
        WARN("Unable to create engine file");
        return false;
    }

    std::unique_ptr<IHostMemory> serializedEngine(_engine->serialize());
    if (!serializedEngine) {
        WARN("Engine serialization failed");
        return false;
    }

    file.write(reinterpret_cast<const char*>(serializedEngine->data()), serializedEngine->size());
    return true;
}

bool CuNNAgent::loadEngine(const String& enginePath)
{
    std::ifstream file(enginePath, std::ios::binary);
    if (!file) {
        WARN("Unable to open engine file");
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    file.read(buffer.data(), size);

    _engine = _runtime->deserializeCudaEngine(buffer.data(), size);
    if (!_engine) {
        WARN("Engine deserialization failed");
        return false;
    }
    return true;
}

bool CuNNAgent::prepareInputSpace()
{
    

    // // 准备输入和输出缓冲区
    // size_t inputSize = 1;
    // for (int i = 0; i < inputDims.nbDims; i++) {
    //     inputSize *= inputDims.d[i];
    // }
    // inputSize *= sizeof(float);

    // size_t outputSize = 1;
    // for (int i = 0; i < outputDims.nbDims; i++) {
    //     outputSize *= outputDims.d[i];
    // }
    // outputSize *= sizeof(float);

    // // 分配GPU内存
    // cudaMalloc(&_gpuInput, inputSize);
    // cudaMalloc(&_gpuOutput, outputSize);

    // // 分配主机内存
    // std::vector<float> h_input(inputSize / sizeof(float));
    // std::vector<float> h_output(outputSize / sizeof(float));
    return true;
}

#else // __USE_CUDA__

OnnxNNAgent::OnnxNNAgent(const String& path, const Ort::Env& env, const Ort::SessionOptions& opt, int classes, const nlohmann::json& params)
#ifdef WIN32
    :_session(env, to_wstring(path.c_str()).c_str(), opt)
#else
    :_session(env, path.c_str(), opt)
#endif
{

}

OnnxNNAgent::~OnnxNNAgent() {

}

int OnnxNNAgent::predict(const DataFeatures& data, Vector<float>& result) {
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtAllocatorType::OrtArenaAllocator, OrtMemTypeDefault);

    if (result.empty())
        return -1;
    return 0;
}

void OnnxNNAgent::train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch) {
    
}

#endif // __USE_CUDA__

NerualNetworkAgentManager& NerualNetworkAgentManager::GetInstance()
{
    static NerualNetworkAgentManager manager;
    return manager;
}

IAgent* NerualNetworkAgentManager::GenerateAgent(const String& path, const nlohmann::json& params)
{
    return new DeepAgent(path, _env, _sessionOptions, 2, params);
}

NerualNetworkAgentManager::NerualNetworkAgentManager()
{
    _sessionOptions.SetIntraOpNumThreads(1);
}
