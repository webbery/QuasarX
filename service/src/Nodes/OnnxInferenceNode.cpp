#include "Nodes/OnnxInferenceNode.h"
#include "server.h"
#include "Util/log.h"
#include "boost/algorithm/string.hpp"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include "Util/string_algorithm.h"

OnnxInferenceNode::OnnxInferenceNode(Server* server)
    : _server(server), _env(ORT_LOGGING_LEVEL_WARNING, "OnnxInference") {}

OnnxInferenceNode::~OnnxInferenceNode() {
    cleanup();
}

void OnnxInferenceNode::cleanup() {
    _session.reset();
}

OnnxObjective OnnxInferenceNode::inferObjective(int outputDim) {
    if (outputDim == 1) return OnnxObjective::Regression;
    if (outputDim == 2) return OnnxObjective::Binary;
    return OnnxObjective::MultiClass;
}

bool OnnxInferenceNode::Init(const nlohmann::json& config) {
    _label = (String)config["label"];

    if (config.contains("params")) {
        auto& p = config["params"];
        if (p.contains("modelFile")) _modelFile = (String)p["modelFile"]["value"];
        if (p.contains("numThreads")) _numThreads = (int)p["numThreads"]["value"];
        if (p.contains("arIndex")) _arIndex = (int)p["arIndex"]["value"];
        if (p.contains("features")) {
            String featStr = (String)p["features"]["value"];
            if (!featStr.empty()) {
                boost::algorithm::split(_featureKeys, featStr, boost::is_any_of(","));
                for (auto& k : _featureKeys) boost::algorithm::trim(k);
            }
        }
    }

    _nFeatures = static_cast<int>(_featureKeys.size());
    if (_nFeatures <= 0) {
        WARN("[OnnxInference] No input features for node {}", _label);
        return false;
    }

    if (_modelFile.empty()) {
        WARN("[OnnxInference] No model file specified for node {}", _label);
        return false;
    }

    // 解析模型路径
    String resolvedPath;
    if (_server) {
        String fullPath = _server->GetConfig().GetDatabasePath() + "/models/" + _modelFile;
        if (std::filesystem::exists(fullPath)) {
            resolvedPath = fullPath;
        }
    }
    if (resolvedPath.empty()) {
        WARN("[OnnxInference] Model file not found for node '{}': {}{}",
             _label, _server ? (_server->GetConfig().GetDatabasePath() + "/models/") : "", _modelFile);
        return false;
    }

    // 创建 ONNX Session
    try {
        _sessionOptions.SetIntraOpNumThreads(
            _numThreads > 0 ? _numThreads : std::max((int)std::thread::hardware_concurrency() / 2, 1));
        _sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

        _session = std::make_unique<Ort::Session>(_env, resolvedPath.c_str(), _sessionOptions);

        // 读取 input shape
        size_t numInputs = _session->GetInputCount();
        if (numInputs != 1) {
            WARN("[OnnxInference] Expected 1 input tensor, got {}", numInputs);
            return false;
        }

        auto inputTypeInfo = _session->GetInputTypeInfo(0);
        auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
        _inputShape = inputTensorInfo.GetShape();

        auto inputNameAllocated = _session->GetInputNameAllocated(0, _allocator);
        _inputNames.push_back(std::string(inputNameAllocated.get()));
        _inputPtrs.push_back(_inputNames.back().c_str());

        // 读取 output shape
        size_t numOutputs = _session->GetOutputCount();
        if (numOutputs != 1) {
            WARN("[OnnxInference] Expected 1 output tensor, got {}", numOutputs);
            return false;
        }

        auto outputTypeInfo = _session->GetOutputTypeInfo(0);
        auto outputTensorInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
        _outputShape = outputTensorInfo.GetShape();

        auto outputNameAllocated = _session->GetOutputNameAllocated(0, _allocator);
        _outputNames.push_back(std::string(outputNameAllocated.get()));
        _outputPtrs.push_back(_outputNames.back().c_str());

        // 计算 lookback 和 outputDim
        int64_t totalInputElements = 1;
        for (auto d : _inputShape) {
            if (d > 0) totalInputElements *= d;
        }
        _lookback = static_cast<int>(totalInputElements / _nFeatures);
        if (_lookback * _nFeatures != totalInputElements) {
            WARN("[OnnxInference] Input shape elements ({}) not divisible by nFeatures ({})",
                 totalInputElements, _nFeatures);
            return false;
        }

        _outputDim = 1;
        if (!_outputShape.empty()) {
            int64_t lastDim = _outputShape.back();
            if (lastDim > 0) _outputDim = static_cast<int>(lastDim);
        }
        _objective = inferObjective(_outputDim);

    } catch (const Ort::Exception& e) {
        WARN("[OnnxInference] Failed to load model '{}': {}", resolvedPath, e.what());
        cleanup();
        return false;
    }

    // 发现 symbol 并解析特征
    auto symbolSet = discoverUpstreamSymbols();
    if (symbolSet.empty()) {
        WARN("[OnnxInference:{}] Cannot discover symbols from upstream connections", _id);
        return false;
    }

    Set<String> symbolStrs;
    for (auto& sym : symbolSet) {
        symbolStrs.insert(get_symbol(sym));
    }

    Map<String, String> allOutKeys;
    for (auto& [handle, nodePtr] : _ins) {
        for (auto& [key, type] : nodePtr->out_elements()) {
            allOutKeys[key] = key;
            for (auto& sym : symbolStrs) {
                String prefix = sym + ".";
                if (key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0) {
                    String field = key.substr(prefix.size());
                    allOutKeys[field] = key;
                    break;
                }
            }
        }
    }

    _outputs.clear();
    for (auto& sym : symbolSet) {
        String symbol = get_symbol(sym);
        Vector<String> resolved;
        for (auto& feat : _featureKeys) {
            if (allOutKeys.count(symbol + "." + feat)) {
                resolved.push_back(allOutKeys[symbol + "." + feat]);
            } else if (allOutKeys.count(feat)) {
                resolved.push_back(allOutKeys[feat]);
            } else {
                resolved.push_back(feat);
            }
        }
        _resolvedFeatures[symbol] = resolved;
        _symbolBuffers[symbol] = std::deque<std::vector<float>>();
        buildOutputs(symbol + ".");
    }

    INFO("[OnnxInference:{}] Loaded '{}' ({} symbols, {} features, lookback={}, outputDim={})",
         _id, _modelFile, symbolSet.size(), _nFeatures, _lookback, _outputDim);
    return true;
}

void OnnxInferenceNode::buildOutputs(const String& symbolPrefix) {
    switch (_objective) {
    case OnnxObjective::Regression:
        _outputs[symbolPrefix + "onnx_pred"] = ArgType::Double_TimeSeries;
        break;
    case OnnxObjective::Binary:
        _outputs[symbolPrefix + "onnx_probs_0"] = ArgType::Double_TimeSeries;
        _outputs[symbolPrefix + "onnx_probs_1"] = ArgType::Double_TimeSeries;
        break;
    case OnnxObjective::MultiClass:
        for (int i = 0; i < _outputDim; i++)
            _outputs[symbolPrefix + "onnx_probs_" + std::to_string(i)] = ArgType::Double_TimeSeries;
        _outputs[symbolPrefix + "onnx_prediction"] = ArgType::Double_TimeSeries;
        break;
    }
}

NodeProcessResult OnnxInferenceNode::Process(const String& strategy, DataContext& context) {
    bool anySuccess = false;

    for (auto& [symbol, resolvedKeys] : _resolvedFeatures) {
        // 收集当前 bar 的特征值
        std::vector<float> features(_nFeatures);
        bool ok = true;
        int validCount = 0;
        String failedFeature;

        for (int d = 0; d < _nFeatures; d++) {
            try {
                const auto& value = context.get(resolvedKeys[d]);
                if (auto* vec = std::get_if<Vector<double>>(&value)) {
                    if (vec->empty()) { ok = false; failedFeature = resolvedKeys[d] + "(empty_vec)"; break; }
                    features[d] = static_cast<float>(vec->back());
                } else if (auto* scalar = std::get_if<double>(&value)) {
                    features[d] = static_cast<float>(*scalar);
                } else {
                    ok = false; failedFeature = resolvedKeys[d] + "(bad_variant)"; break;
                }
                if (std::isfinite(features[d])) ++validCount;
            } catch (...) {
                ok = false; failedFeature = resolvedKeys[d] + "(exception)"; break;
            }
        }

        if (!ok) {
            _lastSkipReason = fmt::format("symbol={} failed at '{}' (valid={}/{})",
                                          symbol, failedFeature, validCount, _nFeatures);
            continue;
        }

        // inf → NaN
        for (int d = 0; d < _nFeatures; d++) {
            if (std::isinf(features[d])) features[d] = NAN;
        }

        // NARX autoregressive 反馈：用上一轮预测填充 arIndex 位置
        auto& buffer = _symbolBuffers[symbol];
        if (_arIndex >= 0 && _arIndex < _nFeatures && !buffer.empty()) {
            // 取上一次推理的预测值填入 autoregressive 特征位
            // 首次推理时 buffer 为空，arIndex 位置保持原始值（通常为 0）
        }

        // 追加到滑动窗口缓冲
        buffer.push_back(features);
        while (static_cast<int>(buffer.size()) > _lookback) {
            buffer.pop_front();
        }

        // 缓冲未满 → 写 NaN 占位
        if (static_cast<int>(buffer.size()) < _lookback) {
            String prefix = symbol + ".";
            auto appendNan = [&context](const String& key) {
                if (context.exist(key)) {
                    context.add(key, std::numeric_limits<double>::quiet_NaN());
                } else {
                    context.set(key, Vector<double>{std::numeric_limits<double>::quiet_NaN()});
                }
            };
            switch (_objective) {
            case OnnxObjective::Regression:
                appendNan(prefix + "onnx_pred");
                break;
            case OnnxObjective::Binary:
                appendNan(prefix + "onnx_probs_0");
                appendNan(prefix + "onnx_probs_1");
                break;
            case OnnxObjective::MultiClass:
                for (int i = 0; i < _outputDim; i++)
                    appendNan(prefix + "onnx_probs_" + std::to_string(i));
                appendNan(prefix + "onnx_prediction");
                break;
            }
            anySuccess = true;
            continue;
        }

        // 构造输入 tensor：展平 buffer → [1, lookback * nFeatures]
        std::vector<float> inputData;
        inputData.reserve(_lookback * _nFeatures);
        for (auto& bar : buffer) {
            inputData.insert(inputData.end(), bar.begin(), bar.end());
        }

        try {
            auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            auto inputTensor = Ort::Value::CreateTensor<float>(
                memoryInfo, inputData.data(), inputData.size(),
                _inputShape.data(), _inputShape.size());

            auto outputTensors = _session->Run(
                Ort::RunOptions{nullptr},
                _inputPtrs.data(), &inputTensor, 1,
                _outputPtrs.data(), 1);

            float* outputData = outputTensors[0].GetTensorMutableData<float>();

            // NARX autoregressive 反馈：将预测值回填到 buffer 的 arIndex 位置
            if (_arIndex >= 0 && _arIndex < _nFeatures && !buffer.empty()) {
                float arValue = outputData[0]; // 回归模式用第一个输出
                buffer.back()[_arIndex] = arValue;
            }

            // 写入 context
            String prefix = symbol + ".";
            switch (_objective) {
            case OnnxObjective::Regression: {
                double val = static_cast<double>(outputData[0]);
                String key = prefix + "onnx_pred";
                if (context.exist(key)) {
                    context.add(key, val);
                } else {
                    context.set(key, Vector<double>{val});
                }
                break;
            }
            case OnnxObjective::Binary: {
                float p1 = outputData[0];
                float p0 = 1.0f - p1;
                String k0 = prefix + "onnx_probs_0";
                String k1 = prefix + "onnx_probs_1";
                if (context.exist(k0)) {
                    context.add(k0, static_cast<double>(p0));
                    context.add(k1, static_cast<double>(p1));
                } else {
                    context.set(k0, Vector<double>{static_cast<double>(p0)});
                    context.set(k1, Vector<double>{static_cast<double>(p1)});
                }
                break;
            }
            case OnnxObjective::MultiClass: {
                int best = 0;
                for (int i = 0; i < _outputDim; i++) {
                    double val = static_cast<double>(outputData[i]);
                    String key = prefix + "onnx_probs_" + std::to_string(i);
                    if (context.exist(key)) {
                        context.add(key, val);
                    } else {
                        context.set(key, Vector<double>{val});
                    }
                    if (outputData[i] > outputData[best]) best = i;
                }
                String predKey = prefix + "onnx_prediction";
                if (context.exist(predKey)) {
                    context.add(predKey, static_cast<double>(best));
                } else {
                    context.set(predKey, Vector<double>{static_cast<double>(best)});
                }
                break;
            }
            }
            anySuccess = true;

        } catch (const Ort::Exception& e) {
            WARN("[OnnxInference:{}] Inference failed for {}: {}", _id, symbol, e.what());
        }
    }

    if (anySuccess) {
        _consecutiveSkipCount = 0;
        return NodeProcessResult::Success;
    }

    ++_consecutiveSkipCount;
    const int maxSkipEpochs = 60;
    if (_consecutiveSkipCount > maxSkipEpochs) {
        FATAL("[OnnxInference:{}] All symbols failed for {} consecutive epochs. Last: {}",
              _id, _consecutiveSkipCount, _lastSkipReason);
        return NodeProcessResult::Error;
    }
    if (_consecutiveSkipCount <= 3 || _consecutiveSkipCount % 20 == 0) {
        WARN("[OnnxInference:{}] All symbols failed ({}/{}), reason: {}",
             _id, _consecutiveSkipCount, maxSkipEpochs, _lastSkipReason);
    }
    return NodeProcessResult::Skip;
}

Map<String, ArgType> OnnxInferenceNode::out_elements() {
    return _outputs;
}

void OnnxInferenceNode::UpdateLabel(const String& label) {
    if (_label != label) {
        Map<String, ArgType> newOutputs;
        for (auto& item : _outputs) {
            String name = item.first;
            boost::algorithm::replace_all(name, _label, label);
            newOutputs[name] = item.second;
        }
        _outputs.swap(newOutputs);
        _label = label;
    }
}

const nlohmann::json OnnxInferenceNode::getParams() {
    return nlohmann::json::object();
}
