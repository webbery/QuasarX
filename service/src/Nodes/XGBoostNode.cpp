#include "Nodes/XGBoostNode.h"
#include "server.h"
#include "Util/log.h"
#include "boost/algorithm/string.hpp"
#include <cstring>

XGBoostNode::XGBoostNode(Server* server) : _server(server) {}

XGBoostNode::~XGBoostNode() {
    cleanup();
}

void XGBoostNode::cleanup() {
    if (_booster) {
        XGBoosterFree(_booster);
        _booster = nullptr;
    }
    _loaded = false;
}

bool XGBoostNode::Init(const nlohmann::json& config) {
    _label = (String)config["label"];

    if (config.contains("params")) {
        auto& p = config["params"];
        if (p.contains("modelFile")) _model_file = (String)p["modelFile"]["value"];
        if (p.contains("objective")) _objective = (String)p["objective"]["value"];
        if (p.contains("num_class")) _num_class = (int)p["num_class"]["value"];
        if (p.contains("features")) {
            String feat_str = (String)p["features"]["value"];
            boost::algorithm::split(_feature_keys, feat_str, boost::is_any_of(","));
            for (auto& k : _feature_keys) boost::algorithm::trim(k);
        }
    }

    if (_feature_keys.empty()) {
        for (auto& item : _ins) {
            auto out_names = item.second->out_elements();
            for (auto& kv : out_names) {
                _feature_keys.push_back(kv.first);
            }
        }
    }

    _n_features = static_cast<int>(_feature_keys.size());
    if (_n_features <= 0) {
        WARN("[XGBoost] No input features for node {}", _label);
        return false;
    }

    if (_model_file.empty()) {
        WARN("[XGBoost] No model file specified for node {}", _label);
        return false;
    }

    // 创建 Booster 并加载模型
    int ret = XGBoosterCreate(nullptr, 0, &_booster);
    if (ret != 0) {
        WARN("[XGBoost] Failed to create booster for node {}", _label);
        return false;
    }

    ret = XGBoosterLoadModel(_booster, _model_file.c_str());
    if (ret != 0) {
        WARN("[XGBoost] Failed to load model '{}' for node {}: {}",
             _model_file, _label, XGBGetLastError());
        cleanup();
        return false;
    }

    buildOutputs();

    INFO("[XGBoost] Loaded model '{}', features={}, objective={}, num_class={}",
         _model_file, _n_features, _objective, _num_class);
    _loaded = true;
    return true;
}

void XGBoostNode::buildOutputs() {
    _outputs.clear();
    if (_objective == "binary:logistic") {
        _outputs["xgb_prob_0"] = ArgType::Double_TimeSeries;
        _outputs["xgb_prob_1"] = ArgType::Double_TimeSeries;
    } else if (_objective == "multi:softprob" || _objective == "multi:softmax") {
        for (int i = 0; i < _num_class; i++) {
            _outputs["xgb_prob_" + std::to_string(i)] = ArgType::Double_TimeSeries;
        }
    } else {
        _outputs["xgb_prediction"] = ArgType::Double_TimeSeries;
    }
}

NodeProcessResult XGBoostNode::Process(const String& strategy, DataContext& context) {
    if (!_loaded) return NodeProcessResult::Skip;

    // 收集当前时刻特征值
    Vector<float> features(_n_features);
    for (int d = 0; d < _n_features; d++) {
        const String& key = _feature_keys[d];
        try {
            const auto& vec = context.get<Vector<double>>(key);
            if (vec.empty()) return NodeProcessResult::Skip;
            features[d] = static_cast<float>(vec.back());
        } catch (...) {
            return NodeProcessResult::Skip;
        }
    }

    // 创建 DMatrix (1 row × n_features)
    DMatrixHandle dmat = nullptr;
    int ret = XGDMatrixCreateFromMat(features.data(), 1, _n_features, NAN, &dmat);
    if (ret != 0) {
        WARN("[XGBoost] Failed to create DMatrix: {}", XGBGetLastError());
        return NodeProcessResult::Skip;
    }

    // 推理
    const char* config = R"({"type": 0, "training": false, "strict_shape": true})";
    bst_ulong const* out_shape = nullptr;
    bst_ulong out_dim = 0;
    const float* out_result = nullptr;

    ret = XGBoosterPredictFromDMatrix(_booster, dmat, config, &out_shape, &out_dim, &out_result);
    XGDMatrixFree(dmat);

    if (ret != 0) {
        WARN("[XGBoost] Prediction failed: {}", XGBGetLastError());
        return NodeProcessResult::Skip;
    }

    // 计算总输出元素数
    bst_ulong total = 1;
    for (bst_ulong i = 0; i < out_dim; i++) total *= out_shape[i];

    // 写入 context
    if (_objective == "binary:logistic") {
        float p1 = (total > 0) ? out_result[0] : 0.0f;
        float p0 = 1.0f - p1;

        if (context.exist("xgb_prob_0")) {
            context.add("xgb_prob_0", static_cast<double>(p0));
            context.add("xgb_prob_1", static_cast<double>(p1));
        } else {
            Vector<double> v0{static_cast<double>(p0)};
            Vector<double> v1{static_cast<double>(p1)};
            context.set("xgb_prob_0", v0);
            context.set("xgb_prob_1", v1);
        }
    } else if (_objective == "multi:softprob" || _objective == "multi:softmax") {
        for (int i = 0; i < _num_class && i < static_cast<int>(total); i++) {
            String key = "xgb_prob_" + std::to_string(i);
            double val = static_cast<double>(out_result[i]);
            if (context.exist(key)) {
                context.add(key, val);
            } else {
                Vector<double> v{val};
                context.set(key, v);
            }
        }
    } else {
        double val = (total > 0) ? static_cast<double>(out_result[0]) : 0.0;
        if (context.exist("xgb_prediction")) {
            context.add("xgb_prediction", val);
        } else {
            Vector<double> v{val};
            context.set("xgb_prediction", v);
        }
    }

    return NodeProcessResult::Success;
}

Map<String, ArgType> XGBoostNode::out_elements() {
    return _outputs;
}

void XGBoostNode::UpdateLabel(const String& label) {
    if (_label != label) {
        Map<String, ArgType> new_outputs;
        for (auto& item : _outputs) {
            String name = item.first;
            boost::algorithm::replace_all(name, _label, label);
            new_outputs[name] = item.second;
        }
        _outputs.swap(new_outputs);
        _label = label;
    }
}

const nlohmann::json XGBoostNode::getParams() {
    return nlohmann::json::object();
}
