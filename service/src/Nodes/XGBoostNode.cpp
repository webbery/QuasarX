#include "Nodes/XGBoostNode.h"
#include "server.h"
#include "Util/log.h"
#include "boost/algorithm/string.hpp"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include "Util/string_algorithm.h"

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
            if (!feat_str.empty()) {
                boost::algorithm::split(_feature_keys, feat_str, boost::is_any_of(","));
                for (auto& k : _feature_keys) boost::algorithm::trim(k);
            }
        }
    }

    if (_feature_keys.empty()) {
        // 按上游节点 id 排序遍历，保证特征顺序稳定（与训练侧一致）
        Set<uint32_t> visited;
        Vector<std::pair<uint32_t, QNode*>> sortedIns;
        for (auto& [handle, nodePtr] : _ins) {
            if (nodePtr && visited.insert(nodePtr->id()).second) {
                sortedIns.push_back({nodePtr->id(), nodePtr});
            }
        }
        std::sort(sortedIns.begin(), sortedIns.end());
        for (auto& [id, nodePtr] : sortedIns) {
            auto out_names = nodePtr->out_elements();
            for (auto& [kv, _] : out_names) {
                _feature_keys.push_back(kv);
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

    // modelFile 存逻辑路径（如 production/xxx.json），实际文件在 {dbPath}/models/ 下
    String resolvedPath;
    if (_server) {
        String fullPath = _server->GetConfig().GetDatabasePath() + "/models/" + _model_file;
#ifdef _WIN32
        // Windows: UTF-8 → UTF-16 才能正确访问中文路径
        std::wstring wFullPath = utf8_to_utf16(fullPath);
        if (std::filesystem::exists(wFullPath)) {
            resolvedPath = fullPath;
        }
#else
        if (std::filesystem::exists(fullPath)) {
            resolvedPath = fullPath;
        }
#endif
    }

    if (resolvedPath.empty()) {
        WARN("[XGBoost] Model file not found for node '{}': looked for '{}{}'",
             _label,
             _server ? (_server->GetConfig().GetDatabasePath() + "/models/") : "",
             _model_file);
        return false;
    }

    // 创建 Booster 并加载模型
    int ret = XGBoosterCreate(nullptr, 0, &_booster);
    if (ret != 0) {
        WARN("[XGBoost] Failed to create booster for node {}", _label);
        return false;
    }

#ifdef _WIN32
    // Windows: 先读入内存再通过 buffer 加载，规避 XGBoost C API 的中文路径问题
    {
        std::wstring wResolved = utf8_to_utf16(resolvedPath);
        std::ifstream ifs(wResolved, std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) {
            WARN("[XGBoost] Cannot open model file '{}' for node {}", resolvedPath, _label);
            cleanup();
            return false;
        }
        auto size = ifs.tellg();
        ifs.seekg(0, std::ios::beg);
        Vector<char> buf(size);
        ifs.read(buf.data(), size);
        ifs.close();
        ret = XGBoosterLoadModelFromBuffer(_booster, buf.data(), buf.size());
    }
#else
    ret = XGBoosterLoadModel(_booster, resolvedPath.c_str());
#endif
    if (ret != 0) {
        WARN("[XGBoost] Failed to load model '{}' for node {}: {}",
             resolvedPath, _label, XGBGetLastError());
        cleanup();
        return false;
    }

    // 从 meta.json 读取训练时的特征顺序，保证推理与训练一致
    // meta 与模型同目录：xxx.json → xxx.meta.json
    if (_feature_keys.empty()) {
        String metaPath = resolvedPath;
        auto dotPos = metaPath.rfind('.');
        if (dotPos != String::npos)
            metaPath = metaPath.substr(0, dotPos) + ".meta.json";
#ifdef _WIN32
        std::wstring wMetaPath = utf8_to_utf16(metaPath);
        bool metaExists = std::filesystem::exists(wMetaPath);
#else
        bool metaExists = std::filesystem::exists(metaPath);
#endif
        if (metaExists) {
            try {
#ifdef _WIN32
                std::ifstream ifs(wMetaPath);
#else
                std::ifstream ifs(metaPath);
#endif
                nlohmann::json meta;
                ifs >> meta;
                if (meta.contains("features") && meta["features"].is_array()) {
                    for (auto& f : meta["features"]) {
                        String fullName = f.get<String>();
                        // 提取短名：sz.800001.MA(5) → MA(5)
                        auto dp = fullName.rfind('.');
                        _feature_keys.push_back(dp != String::npos ? fullName.substr(dp + 1) : fullName);
                    }
                    INFO("[XGBoost:{}] Loaded {} feature names from meta '{}'", _id, _feature_keys.size(), metaPath);
                }
            } catch (const std::exception& e) {
                WARN("[XGBoost:{}] Failed to read meta '{}': {}", _id, metaPath, e.what());
                _feature_keys.clear();
            }
        }
    }

    // ── 从连接图发现 symbol 并解析特征 ──
    // BFS 上游找到所有可达的 QuoteInputNode 的 symbol
    auto symbolSet = discoverUpstreamSymbols();
    if (symbolSet.empty()) {
        WARN("[XGBoost:{}] Cannot discover symbol from upstream connections", _id);
        return false;
    }

    // 构建 symbol 字符串集合，用于从 out_elements key 中提取短名
    Set<String> symbolStrs;
    for (auto& sym : symbolSet) {
        symbolStrs.insert(get_symbol(sym));
    }

    // 收集所有上游 out_elements 的 key，构建 短名→全名 和 全名→全名 双重映射
    Map<String, String> allOutKeys;
    for (auto& [nodeId, nodePtr] : _ins) {
        for (auto& [key, type] : nodePtr->out_elements()) {
            allOutKeys[key] = key;  // 全名: "sz.800001.norm_ret" → itself
            // 从 key 中去掉 symbol 前缀得到短名
            // "sh.600000.cusum.drift" → "cusum.drift"
            // "sh.600000.close"       → "close"
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

    // 为每个 symbol 解析特征 → context key
    _outputs.clear();
    for (auto& sym : symbolSet) {
        String symbol = get_symbol(sym);
        Vector<String> resolved;
        for (auto& feat : _feature_keys) {
            if (allOutKeys.count(symbol + "." + feat)) {
                // symbol 前缀匹配优先（如 "sz.000423.norm_ret"）
                resolved.push_back(allOutKeys[symbol + "." + feat]);
            } else if (allOutKeys.count(feat)) {
                // 短名兜底：全局 key（如 "cusum_signal.drift"、"emd.energy_velocity"）
                resolved.push_back(allOutKeys[feat]);
            } else {
                // 直接作为 context key
                resolved.push_back(feat);
            }
        }
        _resolved_features[symbol] = resolved;
        buildOutputs(symbol + ".");
    }

    INFO("[XGBoost:{}] Loaded '{}', {} symbols, {} features",
         _id, _model_file, symbolSet.size(), _n_features);

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
        _outputs[symbolPrefix + "xgb_prediction"] = ArgType::Double_TimeSeries;
        break;
    case XGBObjective::RegSquaredError:
        _outputs[symbolPrefix + "xgb_prediction"] = ArgType::Double_TimeSeries;
        break;
    }
}

NodeProcessResult XGBoostNode::Process(const String& strategy, DataContext& context) {
    if (!_loaded) {
        if (_consecutiveSkipCount == 0) {
            WARN("[XGBoost:{}] Model not loaded, skipping inference. modelFile='{}'",
                 _id, _model_file);
        }
        ++_consecutiveSkipCount;
        return NodeProcessResult::Skip;
    }

    bool anySuccess = false;

    for (auto& [symbol, resolvedKeys] : _resolved_features) {
        // 收集当前时刻特征值
        Vector<float> features(_n_features);
        bool ok = true;
        int validCount = 0;  // 统计有效 feature 数量（finite）
        String failedFeature;
        for (int d = 0; d < _n_features; d++) {
            try {
                // 兼容时间序列(Vector<double>)和标量(double)两种特征：
                // 时间序列取当前时刻值(vec.back())，标量直接用当前值
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
                DEBUG_INFO("[XGBoost] Read feature {} fail.", resolvedKeys[d]);
                ok = false; failedFeature = resolvedKeys[d] + "(exception)"; break;
            }
        }
        if (!ok) {
            _lastSkipReason = fmt::format("symbol={} failed at '{}' (valid={}/{})",
                                          symbol, failedFeature, validCount, _n_features);
        }
        if (!ok) continue;
        // 有效 feature 不足时跳过推理（早期 epoch 滚动窗口未填满：
        // EMD 120d + ZScore 20d → 前 119 根 K 线 EMD 派生 features 全 NaN），
        // 避免 XGBoost 收到大量 NaN + 少量 finite 走 default branch 输出均匀分布
        // 阈值 80%：15 维特征中至少 12 个有效才推理
        const int minValid = (_n_features * 4 + 4) / 5;  // 80% 向上取整
        if (validCount < minValid) {
            DEBUG_INFO("[XGBoost:{}] skip predict for {}: only {}/{} features valid (need >={}, insufficient warmup)",
                       _id, symbol, validCount, _n_features, minValid);
            // 写 NaN 占位：保持输出序列与特征序列等长同序（DebugNode 按行索引 dump，
            // 若此处不写入，probs 序列会比特征序列短，导致 CSV 行错位）
            String prefix = symbol + ".";
            auto appendNan = [&context](const String& key) {
                if (context.exist(key)) {
                    context.add(key, std::numeric_limits<double>::quiet_NaN());
                } else {
                    context.set(key, Vector<double>{std::numeric_limits<double>::quiet_NaN()});
                }
            };
            switch (_objective) {
            case XGBObjective::BinaryLogistic:
                appendNan(prefix + "xgb_probs_0");
                appendNan(prefix + "xgb_probs_1");
                break;
            case XGBObjective::MultiSoftprob:
            case XGBObjective::MultiSoftmax:
                for (int i = 0; i < _num_class; i++)
                    appendNan(prefix + "xgb_probs_" + std::to_string(i));
                appendNan(prefix + "xgb_prediction");
                break;
            case XGBObjective::RegSquaredError:
                appendNan(prefix + "xgb_prediction");
                break;
            }
            continue;
        }

        // 把 inf 替换为 NaN，让 XGBoost 把它们都识别为 missing
        // 修复: XGBoost 2.x 严格校验 — data 含 inf 但 missing=NaN 会报
        // "Input data contains inf, while missing is not set to inf"
        for (int d = 0; d < _n_features; d++) {
            if (std::isinf(features[d])) features[d] = NAN;
        }

        // 创建 DMatrix (1 row × n_features)
        // missing=NaN: 与 Python xgboost 默认行为一致，NaN/inf 都走 default branch
        DMatrixHandle dmat = nullptr;
        int ret = XGDMatrixCreateFromMat(features.data(), 1, _n_features,
                                          std::numeric_limits<float>::quiet_NaN(), &dmat);
        if (ret != 0) {
            WARN("[XGBoost:{}] Failed to create DMatrix: {}", _id, XGBGetLastError());
            continue;
        }

        // 推理
        bst_ulong const* out_shape = nullptr;
        bst_ulong out_dim = 0;
        const float* out_result = nullptr;

#if XGBOOST_VER_MAJOR >= 2
        // iteration_end=0 表示使用全部迭代（0 在 2.x/3.x 中特殊处理为 BoostedRounds）。
        // 注意：iteration_end=-1 在 XGBoost 2.x C API 中等价于 0 棵树，
        // 输出恒为 base_score 的均匀分布（margin=[0.5,0.5,0.5] → prob=1/3），任何输入都不变。
        const char* config = R"({"type": 0, "training": false, "strict_shape": true, "iteration_begin": 0, "iteration_end": 0})";
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
            // argmax → xgb_prediction
            {
                int best = 0;
                for (int i = 1; i < _num_class && i < static_cast<int>(total); i++) {
                    if (out_result[i] > out_result[best]) best = i;
                }
                String predKey = prefix + "xgb_prediction";
                if (context.exist(predKey)) {
                    context.add(predKey, static_cast<double>(best));
                } else {
                    context.set(predKey, Vector<double>{static_cast<double>(best)});
                }
            }
#ifdef _DEBUG
            // 调试：打印 probs（仅首个 symbol 避免刷屏）
            if (!_resolved_features.empty() && symbol == _resolved_features.begin()->first) {
                auto epoch = context.GetEpoch();
                String msg = "[XGBoost:" + std::to_string(epoch) + "] " + symbol + " probs:";
                for (int i = 0; i < _num_class && i < static_cast<int>(total); i++) {
                    msg += " " + std::to_string(static_cast<double>(out_result[i]));
                }
                INFO("{}", msg);
            }
#endif
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

    if (anySuccess) {
        _consecutiveSkipCount = 0;
        return NodeProcessResult::Success;
    }

    ++_consecutiveSkipCount;
    // 预热期内允许 Skip（特征窗口尚未填满），超过阈值后视为持续性故障
    const int maxSkipEpochs = 60;
    if (_consecutiveSkipCount > maxSkipEpochs) {
        FATAL("[XGBoost:{}] All symbols failed feature collection for {} consecutive epochs (limit={}). "
              "Last reason: {}. Check upstream node data availability.",
              _id, _consecutiveSkipCount, maxSkipEpochs, _lastSkipReason);
        return NodeProcessResult::Error;
    }

    if (_consecutiveSkipCount <= 3 || _consecutiveSkipCount % 20 == 0) {
        WARN("[XGBoost:{}] All symbols failed feature collection ({}/{} epochs), reason: {}",
             _id, _consecutiveSkipCount, maxSkipEpochs, _lastSkipReason);
    }
    return NodeProcessResult::Skip;
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
