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

XGBObjective XGBoostNode::parseObjective(const String& s) {
    if (s == "binary:logistic") return XGBObjective::BinaryLogistic;
    if (s == "multi:softprob") return XGBObjective::MultiSoftprob;
    if (s == "multi:softmax") return XGBObjective::MultiSoftmax;
    if (s == "reg:squarederror") return XGBObjective::RegSquaredError;
    WARN("[XGBoost] Unknown objective '{}', defaulting to multi:softprob", s);
    return XGBObjective::MultiSoftprob;
}

bool XGBoostNode::Init(const nlohmann::json& config) {
    _label = (String)config["label"];

    if (config.contains("params")) {
        auto& p = config["params"];
        if (p.contains("modelFile")) _model_file = (String)p["modelFile"]["value"];
        if (p.contains("objective")) _objective = parseObjective((String)p["objective"]["value"]);
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

    // ── 从连接图发现 symbol 并解析特征 ──
    // 收集所有上游 out_elements 的 key，构建 短名→全名 和 全名→全名 双重映射
    Map<String, String> allOutKeys;
    Set<String> symbols;
    for (auto& [nodeId, nodePtr] : _ins) {
        for (auto& [key, type] : nodePtr->out_elements()) {
            allOutKeys[key] = key;  // 全名: "sz.800001.norm_ret" → itself
            auto dotPos = key.rfind('.');
            if (dotPos != String::npos) {
                String field = key.substr(dotPos + 1);
                allOutKeys[field] = key;  // 短名: "norm_ret" → "sz.800001.norm_ret"
                if (field == "close" || field == "open" || field == "volume")
                    symbols.insert(key.substr(0, dotPos));
            }
        }
    }

    if (symbols.empty()) {
        WARN("[XGBoost:{}] Cannot discover symbol from upstream connections", _id);
        return false;
    }

    // 为每个 symbol 解析特征 → context key
    _outputs.clear();
    for (auto& symbol : symbols) {
        Vector<String> resolved;
        for (auto& feat : _feature_keys) {
            if (allOutKeys.count(feat)) {
                // 精确匹配（如 "cusum_signal.drift" 或 "norm_ret"）
                resolved.push_back(allOutKeys[feat]);
            } else if (allOutKeys.count(symbol + "." + feat)) {
                // symbol + feat 拼接（兜底）
                resolved.push_back(symbol + "." + feat);
            } else {
                // 直接作为 context key（全局 key）
                resolved.push_back(feat);
            }
        }
        _resolved_features[symbol] = resolved;
        buildOutputs(symbol + ".");
    }

    INFO("[XGBoost:{}] Loaded '{}', {} symbols, {} features",
         _id, _model_file, symbols.size(), _n_features);

    _loaded = true;
    return true;
}

void XGBoostNode::buildOutputs(const String& symbolPrefix) {
    switch (_objective) {
    case XGBObjective::BinaryLogistic:
        _outputs[symbolPrefix + "xgb_probs_0"] = ArgType::Double_TimeSeries;
        _outputs[symbolPrefix + "xgb_probs_1"] = ArgType::Double_TimeSeries;
        break;
    case XGBObjective::MultiSoftprob:
    case XGBObjective::MultiSoftmax:
        for (int i = 0; i < _num_class; i++)
            _outputs[symbolPrefix + "xgb_probs_" + std::to_string(i)] = ArgType::Double_TimeSeries;
        break;
    case XGBObjective::RegSquaredError:
        _outputs[symbolPrefix + "xgb_prediction"] = ArgType::Double_TimeSeries;
        break;
    }
}

NodeProcessResult XGBoostNode::Process(const String& strategy, DataContext& context) {
    if (!_loaded) return NodeProcessResult::Skip;

    bool anySuccess = false;

    for (auto& [symbol, resolvedKeys] : _resolved_features) {
        // 收集当前时刻特征值
        Vector<float> features(_n_features);
        bool ok = true;
        for (int d = 0; d < _n_features; d++) {
            try {
                const auto& vec = context.get<Vector<double>>(resolvedKeys[d]);
                if (vec.empty()) { ok = false; break; }
                features[d] = static_cast<float>(vec.back());
            } catch (...) {
                ok = false; break;
            }
        }
        if (!ok) continue;

        // 创建 DMatrix (1 row × n_features)
        DMatrixHandle dmat = nullptr;
        int ret = XGDMatrixCreateFromMat(features.data(), 1, _n_features, NAN, &dmat);
        if (ret != 0) {
            WARN("[XGBoost:{}] Failed to create DMatrix: {}", _id, XGBGetLastError());
            continue;
        }

        // 推理
        bst_ulong const* out_shape = nullptr;
        bst_ulong out_dim = 0;
        const float* out_result = nullptr;

#if XGBOOST_VER_MAJOR >= 2
        const char* config = R"({"type": 0, "training": false, "strict_shape": true})";
        ret = XGBoosterPredictFromDMatrix(_booster, dmat, config, &out_shape, &out_dim, &out_result);
#else
        bst_ulong out_shape_val = 0;
        bst_ulong out_dim_val = 0;
        float* out_result_mut = nullptr;
        ret = XGBoosterPredictFromDMatrix(_booster, dmat,
                                           0, 0, 0, &out_shape_val, &out_dim_val, &out_result_mut);
        out_shape = &out_shape_val;
        out_dim = out_dim_val;
        out_result = out_result_mut;
#endif
        XGDMatrixFree(dmat);

        if (ret != 0) {
            WARN("[XGBoost:{}] Prediction failed for {}: {}", _id, symbol, XGBGetLastError());
            continue;
        }

        bst_ulong total = 1;
        for (bst_ulong i = 0; i < out_dim; i++) total *= out_shape[i];

        // 写入 context（带 symbol 前缀，xgb_probs_N 命名）
        String prefix = symbol + ".";
        switch (_objective) {
        case XGBObjective::BinaryLogistic: {
            float p1 = (total > 0) ? out_result[0] : 0.0f;
            float p0 = 1.0f - p1;
            String k0 = prefix + "xgb_probs_0";
            String k1 = prefix + "xgb_probs_1";
            if (context.exist(k0)) {
                context.add(k0, static_cast<double>(p0));
                context.add(k1, static_cast<double>(p1));
            } else {
                context.set(k0, Vector<double>{static_cast<double>(p0)});
                context.set(k1, Vector<double>{static_cast<double>(p1)});
            }
            break;
        }
        case XGBObjective::MultiSoftprob:
        case XGBObjective::MultiSoftmax:
            for (int i = 0; i < _num_class && i < static_cast<int>(total); i++) {
                String key = prefix + "xgb_probs_" + std::to_string(i);
                double val = static_cast<double>(out_result[i]);
                if (context.exist(key)) {
                    context.add(key, val);
                } else {
                    context.set(key, Vector<double>{val});
                }
            }
            break;
        case XGBObjective::RegSquaredError: {
            double val = (total > 0) ? static_cast<double>(out_result[0]) : 0.0;
            String key = prefix + "xgb_prediction";
            if (context.exist(key)) {
                context.add(key, val);
            } else {
                context.set(key, Vector<double>{val});
            }
            break;
        }
        }
        anySuccess = true;
    }

    return anySuccess ? NodeProcessResult::Success : NodeProcessResult::Skip;
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
