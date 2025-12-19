#include "Nodes/NeuralNetworkNode.h"
#include "onnxruntime/onnxruntime_cxx_api.h"
#include <algorithm>
#include <cstring>
#include <thread>
#include "Util/system.h"

using namespace Ort;
Env ArtificialIntelligenceNode::_env(ORT_LOGGING_LEVEL_WARNING, "AIInference");

bool LSTMNode::Init(const nlohmann::json& config) {
    // 
    String model_org_path = config["params"]["model"]["value"];
    String server_model_path = ConvertServerModelPath(model_org_path);

    SessionOptions session_options;
    int num_thread = std::max((int)std::thread::hardware_concurrency() / 2, 1);
    session_options.SetIntraOpNumThreads(num_thread);  // 设置线程数
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
    if (!_session) {
#ifdef _WIN32
        auto model_path = to_wstring(server_model_path.c_str());
        _session = new Session(_env, model_path.c_str(), session_options);
#else
        _session = new Session(_env, server_model_path.c_str(), session_options);
#endif
    }
    // 获取预测的窗口大小
    _predictWindow = (int)config["params"]["step"]["value"];
    // 输入窗口大小, 0表示默认为4
    if (config["params"].contains("input")) {
        int value = (int)config["params"]["input"]["value"];
        if (value != 0) {
            _inputWindow = value;
        }
    }
    InitInput();
    InitOutput();

    for (auto& item: _ins) {
        auto out_name = item.second->out_elements();
        for (auto& info: out_name) {
            _inputNames.push_back(info.first);
        }
    }
    return true;
}

bool LSTMNode::Process(const String& strategy, DataContext& context) {
    Vector<float> input;
    int offset = 0;
    for (auto& featureName: _inputNames) {
        Vector<double>& org_data = std::get<Vector<double>>(context.get(featureName));
        if (org_data.size() < _inputWindow)
            return true;

        if (input.empty()) {
            input.resize(org_data.size() * _inputNames.size());
        }
        std::transform(org_data.begin(), org_data.end(), input.begin() + offset,
            [](double val) {return static_cast<float>(val);});
    }
    
    Vector<int64_t> input_shape{1, _inputWindow, (int64_t)_inputNames.size()};  // batch=1, seq_len=10, features=1
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input.data(), input.size(),
        input_shape.data(), input_shape.size());
        
    auto output_tensors = _session->Run(
        Ort::RunOptions{nullptr},
        (const char *const *)&(_modelInputs[0]), &input_tensor, 1,
        (const char *const *)&(_modelOutputs[0]), 1);

    int i = 0;
    for (auto& item: output_tensors) {
        size_t cnt = 0;
        auto shape = item.GetTensorTypeAndShapeInfo().GetShape();
        std::for_each(shape.begin(), shape.end(), [&cnt] (int64_t dim) {
            cnt *= dim;
        });
        float* raw_data = item.GetTensorMutableData<float>();
        std::vector<float> output_data(raw_data, raw_data + cnt);
        // context.set(_outputName + ":" + std::to_string(i++), output_data);
    }
    return true;
}

LSTMNode::~LSTMNode() {
    if (!_modelInputs.empty()) {
        for (auto name: _modelInputs) {
            delete[] name;
        }
    }
    if (!_modelOutputs.empty()) {
        for (auto name: _modelOutputs) {
            delete[] name;
        }
    }
    if (_session) {
        delete _session;
        _session = nullptr;
    }
}

const nlohmann::json LSTMNode::getParams() {
    return {"model", "step", "input"};
}