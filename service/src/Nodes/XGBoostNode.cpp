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

void XGBoostNode::writeNaNPlaceholders(DataContext& context, const String& symbol) const {
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
}

void XGBoostNode::writeBatchPredictions(DataContext& context, const String& symbol,
                                         const float* row, int colsPerRow) const {
    String prefix = symbol + ".";
    switch (_objective) {
    case XGBObjective::BinaryLogistic: {
        float p1 = (colsPerRow > 0) ? row[0] : 0.0f;
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
        for (int i = 0; i < _num_class && i < colsPerRow; i++) {
            String key = prefix + "xgb_probs_" + std::to_string(i);
            double val = static_cast<double>(row[i]);
            if (context.exist(key)) {
                context.add(key, val);
            } else {
                context.set(key, Vector<double>{val});
            }
        }
        // argmax → xgb_prediction
        {
            int best = 0;
            for (int i = 1; i < _num_class && i < colsPerRow; i++) {
                if (row[i] > row[best]) best = i;
            }
            String predKey = prefix + "xgb_prediction";
            if (context.exist(predKey)) {
                context.add(predKey, static_cast<double>(best));
            } else {
                context.set(predKey, Vector<double>{static_cast<double>(best)});
            }
        }
        break;
    case XGBObjective::RegSquaredError: {
        double val = (colsPerRow > 0) ? static_cast<double>(row[0]) : 0.0;
        String key = prefix + "xgb_prediction";
        if (context.exist(key)) {
            context.add(key, val);
        } else {
            context.set(key, Vector<double>{val});
        }
        break;
    }
    }
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
        WARN("[XGBoost] No model file specified for node {}, skipping model load", _label);
    } else {
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

        // 从模型读取训练时的特征顺序（权威来源），保证推理与训练一致
        // 模型 feature_names 是训练时 DMatrix 传入的顺序，优先级最高
        {
            bst_ulong model_n_feat = 0;
            const char** model_feat_names = nullptr;
            int ret_feat = XGBoosterGetStrFeatureInfo(_booster, "feature_name",
                                                       &model_n_feat, &model_feat_names);
            if (ret_feat == 0 && model_n_feat > 0 && model_feat_names) {
                Vector<String> model_features;
                for (bst_ulong i = 0; i < model_n_feat; i++) {
                    if (model_feat_names[i] && model_feat_names[i][0] != '\0')
                        model_features.push_back(model_feat_names[i]);
                }
                if (model_features.size() == static_cast<size_t>(_n_features)) {
                    INFO("[XGBoost:{}] Using model feature_names ({} features), overriding params.features order",
                         _id, model_n_feat);
                    _feature_keys = std::move(model_features);
                } else {
                    WARN("[XGBoost:{}] Model has {} features but node config has {} features, "
                         "keeping params.features order", _id, model_n_feat, _n_features);
                }
            }
        }

        // meta.json 作为第二回退（仅当模型无 feature_names 且 params.features 为空时）
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
    } // else: model_file not empty

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
    Set<String> unresolvedFeatures;  // 跟踪无法解析的特征
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
                // 无法解析：记录并继续使用原始名称（将在 Process 时报错）
                unresolvedFeatures.insert(feat);
                resolved.push_back(feat);
            }
        }
        _resolved_features[symbol] = resolved;
        buildOutputs(symbol + ".");
    }

    // 如果有无法解析的特征，终止初始化
    if (!unresolvedFeatures.empty()) {
        String unresolvedList = boost::algorithm::join(unresolvedFeatures, ", ");
        Vector<String> availableKeys;
        for (auto& [k, v] : allOutKeys) availableKeys.push_back(k);
        String availableList = boost::algorithm::join(availableKeys, ", ");

        String errorMsg = fmt::format(
            "[XGBoost:{}] Init FAILED: {} features could not be resolved from upstream nodes: {}. "
            "Available upstream outputs: [{}]. "
            "Fix: ensure feature names in model config match upstream node output keys "
            "(e.g. EMD node outputs 'emd.nimf_0', not 'nimf_0').",
            _id, unresolvedFeatures.size(), unresolvedList, availableList);
        WARN("{}", errorMsg);

        // 通过 strategy_log 通知前端（Init 阶段无 strategy name，用节点 id 标识）
        strategy_log(fmt::format("xgboost_node_{}", _id), errorMsg);

        return false;
    }

    INFO("[XGBoost:{}] {} '{}', {} symbols, {} features",
         _id, _model_file.empty() ? "Initialized (no model)" : "Loaded",
         _model_file, symbolSet.size(), _n_features);

    _loaded = !_model_file.empty();
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

    // ── Pass 1: 收集每个 symbol 的特征 ──
    //   - 收集异常 (!ok): 与旧逻辑一致——仅 continue，不写 NaN 占位、不 ++_consecutiveSkipCount
    //   - 部分特征无效 (ok && validCount<_n_features): 预热期静默，否则计数并可能终止；写 NaN 占位
    //   - 全部有效: 把特征向量追加进 batch 矩阵，记录其 symbol 顺序
    //
    // batch 是一段连续存储的 N_valid × _n_features 矩阵（行主序），batchOrder[i] 是第 i 行的 symbol。
    Vector<float> batch;
    Vector<const String*> batchOrder;

    for (auto& [symbol, resolvedKeys] : _resolved_features) {
        Vector<float> features(_n_features);
        bool ok = true;
        int validCount = 0;
        String failedFeature;
        for (int d = 0; d < _n_features; d++) {
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
                DEBUG_INFO("[XGBoost] Read feature {} fail.", resolvedKeys[d]);
                ok = false; failedFeature = resolvedKeys[d] + "(exception)"; break;
            }
        }
        if (!ok) {
            _lastSkipReason = fmt::format("symbol={} failed at '{}' (valid={}/{})",
                                          symbol, failedFeature, validCount, _n_features);
        }

        if (!ok) {
            // 收集异常：与旧行为一致——不写 NaN 占位、不 ++_consecutiveSkipCount
            continue;
        }

        if (validCount < _n_features) {
            if (context.IsInWarmup()) {
                DEBUG_INFO("[XGBoost:{}] skip predict for {}: only {}/{} features valid (warmup epoch {})",
                           _id, symbol, validCount, _n_features, context.GetEpoch());
            } else {
                String errorMsg = fmt::format(
                    "[XGBoost:{}] symbol {} has only {}/{} valid features at epoch {} (past warmup). "
                    "Feature '{}' is not finite. "
                    "This means the upstream node is not producing valid output for this feature. "
                    "Check node connections and feature names.",
                    _id, symbol, validCount, _n_features, context.GetEpoch(), failedFeature);
                WARN("{}", errorMsg);
                strategy_log(strategy, errorMsg);

                _consecutiveSkipCount++;
                if (_consecutiveSkipCount > 50) {
                    WARN("[XGBoost:{}] Persistent feature invalid after {} consecutive skips. "
                          "Terminating inference. Last reason: {}",
                          _id, _consecutiveSkipCount, _lastSkipReason);
                    strategy_log(strategy, fmt::format(
                        "[XGBoost:{}] Terminated: {} consecutive feature failures. "
                        "Fix feature issues and restart.", _id, _consecutiveSkipCount));
                    return NodeProcessResult::Error;
                }
            }
            writeNaNPlaceholders(context, symbol);
            anySuccess = true;
            continue;
        }

        // 全部有效：重置计数、把 inf 替换为 NaN，加入 batch
        _consecutiveSkipCount = 0;
        for (int d = 0; d < _n_features; d++) {
            if (std::isinf(features[d])) features[d] = NAN;
        }
        batch.insert(batch.end(), features.begin(), features.end());
        batchOrder.push_back(&symbol);
    }

    int N = static_cast<int>(batchOrder.size());
    if (N == 0) {
        // 没有可推理的 symbol。
        // 若 Pass 1 已有 NaN 占位（anySuccess==true），说明节点这一轮并非空转——与旧实现一致返回 Success；
        // 否则视为 all-fail（持续 skip 计数 + 检查终止阈值）。
        if (anySuccess) {
            _consecutiveSkipCount = 0;
            return NodeProcessResult::Success;
        }
        ++_consecutiveSkipCount;
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

    // ── Pass 2: 批量推理（一次 DMatrix + 一次 Predict + 一次 Free） ──
    // N==1 时 batch 与旧实现的 1×F 输入等价，输出 bit-identical；
    // N>1 时省掉 N-1 次固定 C API 开销 + XGBoost 内部 dispatch。
    // missing=NaN: 与 Python xgboost 默认行为一致，NaN/inf 都走 default branch
    DMatrixHandle dmat = nullptr;
    int ret = XGDMatrixCreateFromMat(batch.data(), N, _n_features,
                                     std::numeric_limits<float>::quiet_NaN(), &dmat);
    if (ret != 0) {
        WARN("[XGBoost:{}] Failed to create DMatrix: {}", _id, XGBGetLastError());
        // DMatrix 失败：若 Pass 1 已有 NaN 占位，仍视为 Success（与旧实现 "anySuccess=true→reset→Success" 一致）；
        // 否则按 all-fail 处理。
        if (anySuccess) {
            _consecutiveSkipCount = 0;
            return NodeProcessResult::Success;
        }
        ++_consecutiveSkipCount;
        return NodeProcessResult::Skip;
    }

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
        WARN("[XGBoost:{}] Prediction failed: {}", _id, XGBGetLastError());
        // 同 DMatrix 失败的处理：若 Pass 1 已写过 NaN，节点并非空转，返回 Success。
        if (anySuccess) {
            _consecutiveSkipCount = 0;
            return NodeProcessResult::Success;
        }
        ++_consecutiveSkipCount;
        return NodeProcessResult::Skip;
    }

    bst_ulong total = 1;
    for (bst_ulong i = 0; i < out_dim; i++) total *= out_shape[i];

    // ── Pass 3: 按 batchOrder 顺序回写每个 symbol 的结果 ──
    // out_result 布局：(out_dim × N)，out_shape = [N, colsPerRow]
    //   binary:logistic  → colsPerRow = 1（total = N）
    //   multi:softprob   → colsPerRow = n_class（total = N * n_class）
    //   reg:squarederror → colsPerRow = 1（total = N）
    int colsPerRow = static_cast<int>(total / N);
    for (int i = 0; i < N; i++) {
        const String& symbol = *batchOrder[i];
        const float* rowPtr = out_result + i * colsPerRow;
        writeBatchPredictions(context, symbol, rowPtr, colsPerRow);
    }

    // Pass 3 成功完成：标记 anySuccess（与旧实现"在循环末尾隐式设 true"等价）。
    // 注意：若 Pass 1 已有 NaN 占位，anySuccess 早已是 true；此处覆盖不会影响计数语义，
    // 因为 Pass 1 的有效分支已把 _consecutiveSkipCount 重置为 0。
    anySuccess = true;

    // 能走到这里，说明 N>0 且 Pass 2/3 全成功——必然返回 Success。
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
