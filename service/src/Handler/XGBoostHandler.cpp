#include "Handler/XGBoostHandler.h"
#include "AgentSubSystem.h"
#include "Strategy.h"
#include "StrategyNode.h"
#include "ExchangeManager.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/exchange.h"
#include "Nodes/QuoteNode.h"
#include "Nodes/XGBoostNode.h"
#include "Util/PythonRunner.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/datetime.h"
#include "server.h"
#include "std_header.h"
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <filesystem>
#ifdef WIN32
#else
#include <unistd.h>
#endif

extern "C" {
#include <xgboost/c_api.h>
}

namespace {

String makeTempPath(const String& prefix, const String& ext) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    return fmt::format("/tmp/{}_{}_{}.{}", prefix, getpid(), gen() & 0xFFFFFF, ext);
}

bool writeCsv(const String& path, const Map<String, Vector<double>>& data, const Vector<String>& dates) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    // 写表头：date 列 + 数据列
    bool hasDates = !dates.empty();
    if (hasDates) ofs << "date,";
    bool first = true;
    for (auto& [k, _] : data) {
        if (!first) ofs << ",";
        ofs << k;
        first = false;
    }
    ofs << "\n";
    size_t rows = 0;
    for (auto& [_, v] : data) {
        if (!v.empty()) { rows = v.size(); break; }
    }
    for (size_t i = 0; i < rows; ++i) {
        first = true;
        if (hasDates) {
            ofs << (i < dates.size() ? dates[i] : "");
            ofs << ",";
        }
        for (auto& [k, v] : data) {
            if (!first) ofs << ",";
            double val = (i < v.size()) ? v[i] : 0.0;
            if (val != val) ofs << "";
            else ofs << val;
            first = false;
        }
        ofs << "\n";
    }
    return true;
}

Set<String> sourcesFromNodes(const List<QNode*>& nodes) {
    Set<String> sources;
    for (auto node : nodes) {
        if (dynamic_cast<QuoteInputNode*>(node)) {
            sources.insert("股票");  // 默认股票源
        }
    }
    if (sources.empty()) sources.insert("股票");
    return sources;
}

}  // namespace

uint64_t XGBoostHandler::registerModel(BoosterHandle booster, Vector<String> features, Vector<Vector<double>> x_test) {
    std::lock_guard<std::mutex> lock(_mtx);
    uint64_t id = _nextId.fetch_add(1);
    _cache[id].booster = booster;
    _cache[id].features = std::move(features);
    _cache[id].X_test = std::move(x_test);
    return id;
}

CachedXGBoostModel* XGBoostHandler::getModel(uint64_t id) {
    std::lock_guard<std::mutex> lock(_mtx);
    auto itr = _cache.find(id);
    return itr == _cache.end() ? nullptr : &itr->second;
}

bool XGBoostHandler::deleteModel(uint64_t id) {
    std::lock_guard<std::mutex> lock(_mtx);
    auto itr = _cache.find(id);
    if (itr == _cache.end()) return false;
    itr->second.clear();
    _cache.erase(itr);
    return true;
}

// ============ 各个 action 的处理函数 ============

void XGBoostHandler::handleTrain(const nlohmann::json& params, httplib::Response& res) {
    // ============== 1. 解析请求 ==============
    String strScript = params.value("script", "");
    if (strScript.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing 'script'"})", "application/json");
        return;
    }
    nlohmann::json script;
    try {
        script = nlohmann::json::parse(strScript);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid strategy script JSON"})", "application/json");
        return;
    }

    nlohmann::json labelCfg = params.value("label", nlohmann::json::object());
    nlohmann::json xgbParams = params.value("params", nlohmann::json::object());
    nlohmann::json dateRangeCfg = params.value("date_range", nlohmann::json::object());
    double testRatio = params.value("test_ratio", 0.2);

    String labelSource = labelCfg.value("source", "");
    int labelPeriod = labelCfg.value("period", 5);
    String labelType = labelCfg.value("type", "classification");
    double volK = labelCfg.value("vol_k", 0.5);
    String objective = params.value("objective", labelType == "classification" ? "multi:softprob" : "reg:squarederror");
    int numClass = params.value("num_class", 3);

    // 日期范围和频率（与标签分析一致）
    String startDate = dateRangeCfg.value("start", "");
    String endDate = dateRangeCfg.value("end", "");
    String frequency = dateRangeCfg.value("frequency", "1d");

    String strategyName = script.value("id", "xgboost_train");
    if (labelSource.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing label.source"})", "application/json");
        return;
    }

    // ============== 2. 解析策略图 + 提取上游子图 ==============
    List<QNode*> fullGraph;
    Set<QNode*> upstreamSet;
    try {
        fullGraph = parse_strategy_script_v2(script, _server);
        fullGraph = topo_sort(fullGraph);
    } catch (const std::exception& e) {
        for (auto n : fullGraph) delete n;
        res.status = 400;
        String msg = R"({"message":"strategy parse failed: )" + String(e.what()) + R"("})";
        res.set_content(msg.c_str(), "application/json");
        return;
    }

    upstreamSet = collectUpstreamNodes(fullGraph);
    if (upstreamSet.empty()) {
        for (auto n : fullGraph) delete n;
        res.status = 400;
        res.set_content(R"({"message":"未找到 XGBoost 节点或上游子图为空"})", "application/json");
        return;
    }

    List<QNode*> upstreamSubgraph;
    for (auto n : fullGraph) {
        if (upstreamSet.count(n)) upstreamSubgraph.push_back(n);
    }

    // ============== 3. Init 上游节点 + 提取特征列 ==============
    Vector<String> featureNames;
    try {
        std::map<uint32_t, nlohmann::json> nodeConfigMap;
        for (auto& node : script["nodes"]) {
            uint32_t id = atoi(node["id"].get<std::string>().c_str());
            nodeConfigMap[id] = node["data"];
        }
        for (auto n : upstreamSubgraph) {
            auto cfgItr = nodeConfigMap.find(n->id());
            if (cfgItr != nodeConfigMap.end()) {
                n->Init(cfgItr->second);
            }
        }
        for (auto n : upstreamSubgraph) {
            auto elements = n->out_elements();
            for (auto& [k, _] : elements) featureNames.push_back(k);
        }
    } catch (const std::exception& e) {
        for (auto n : fullGraph) delete n;
        res.status = 400;
        String msg = R"({"message":"node init failed: )" + String(e.what()) + R"("})";
        res.set_content(msg.c_str(), "application/json");
        return;
    }

    // ============== 4. 启动 Exchange + 创建回测上下文 ==============
    auto* exchangeMgr = _server->GetExchangeManager();
    if (!exchangeMgr) {
        for (auto n : fullGraph) delete n;
        res.status = 500;
        res.set_content(R"({"message":"ExchangeManager unavailable"})", "application/json");
        return;
    }

    Set<String> requiredSources = sourcesFromNodes(upstreamSubgraph);
    exchangeMgr->StartRequiredExchanges(requiredSources);

    Set<symbol_t> symbols;
    for (auto n : upstreamSubgraph) {
        if (auto* qn = dynamic_cast<QuoteInputNode*>(n)) {
            auto& syms = qn->GetSymbols();
            for (auto s : syms) symbols.insert(s);
        }
    }
    if (symbols.empty()) {
        for (auto n : fullGraph) delete n;
        res.status = 400;
        res.set_content(R"({"message":"未找到可用的 symbols"})", "application/json");
        return;
    }

    // ============== 5. 部分回测 + 数据收集 ==============
    auto* flowSubsystem = _server->GetStrategySystem()->GetFlowSubsystem();
    double initialCapital = 100000.0;
    String tmpStrategyName = strategyName + "_train";

    Map<String, Vector<double>> collected;
    Vector<String> collectedDates;
    bool collectOk = flowSubsystem->RunTrainingCollect(
        tmpStrategyName, upstreamSubgraph, requiredSources,
        symbols, initialCapital, collected, collectedDates);

    if (!collectOk || collected.empty()) {
        for (auto n : fullGraph) delete n;
        res.status = 500;
        res.set_content(R"({"message":"数据收集失败，请确认 Quote 节点已配置标的数据"})", "application/json");
        return;
    }

    if (collected.find(labelSource) == collected.end()) {
        String availableKeys;
        int cnt = 0;
        for (auto& [k, _] : collected) {
            if (cnt++ > 0) availableKeys += ", ";
            availableKeys += k;
        }
        for (auto n : fullGraph) delete n;
        res.status = 400;
        String msg = R"({"message":"label.source ')" + labelSource + R"(' not in collected data. Available: )" + availableKeys + R"("})";
        res.set_content(msg.c_str(), "application/json");
        return;
    }

    // ============== 6. 写出临时 CSV + 调用 Python 训练 ==============
    String csvPath = makeTempPath("xgb_data", "csv");
    String modelPath = makeTempPath("xgb_model", "json");

    writeCsv(csvPath, collected, collectedDates);

    auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
    auto interpreter = pyEnv.resolve(params.value("py_env", ""));

    std::vector<std::string> args = {
        "--data", csvPath,
        "--label-source", labelSource,
        "--label-period", std::to_string(labelPeriod),
        "--label-type", labelType,
        "--vol-k", std::to_string(volK),
        "--objective", objective,
        "--num-class", std::to_string(numClass),
        "--model-output", modelPath,
        "--params", xgbParams.dump(),
        "--test-ratio", std::to_string(testRatio),
        "--start-date", startDate,
        "--end-date", endDate,
        "--frequency", frequency,
    };

    String scriptPath = "tools/xgboost_train.py";

    PythonRunner runner;
    if (!runner.start(scriptPath, args, interpreter)) {
        for (auto n : fullGraph) delete n;
        res.status = 500;
        res.set_content(R"({"message":"failed to start training script"})", "application/json");
        return;
    }

    PythonOutput out;
    String resultLine;
    String stderrLines;
    while (runner.readLine(out, 60000)) {
        if (out.type == PythonOutput::DONE) break;
        if (out.type == PythonOutput::STDOUT) {
            if (out.line.find("\"type\":\"result\"") != std::string::npos) {
                resultLine = out.line;
            } else if (out.line.find("\"type\":\"progress\"") != std::string::npos) {
                INFO("[XGBoostTrain] {}", out.line);
            }
        } else if (out.type == PythonOutput::STDERR) {
            WARN("[XGBoostTrain stderr] {}", out.line);
            if (stderrLines.size() < 1000) {
                stderrLines += out.line + "\n";
            }
        }
    }

    for (auto n : fullGraph) delete n;

    if (resultLine.empty()) {
        res.status = 500;
        String msg = stderrLines.empty() ? "训练脚本未输出 result" : stderrLines.substr(0, 500);
        // 去除末尾换行
        while (!msg.empty() && msg.back() == '\n') msg.pop_back();
        nlohmann::json errResp = {{"message", "训练失败: " + msg}};
        res.set_content(errResp.dump(), "application/json");
        std::remove(csvPath.c_str());
        return;
    }

    nlohmann::json trainResult;
    try {
        trainResult = nlohmann::json::parse(resultLine);
    } catch (...) {
        res.status = 500;
        res.set_content(R"({"message":"训练结果解析失败: )" + resultLine.substr(0, 200) + R"("})", "application/json");
        std::remove(csvPath.c_str());
        std::remove(modelPath.c_str());
        return;
    }

    // ============== 7. 持久化模型到 experiments/ ==============
    String dbPath = _server->GetConfig().GetDatabasePath();
    String expDir = dbPath + "/models/experiments";
    std::filesystem::create_directories(expDir);

    time_t now = Now();
    String ts = ToString(now, "%Y%m%d_%H%M%S");
    String persistName = strategyName + "_" + ts;
    String persistPath = expDir + "/" + persistName + ".json";
    std::filesystem::copy(modelPath, persistPath, std::filesystem::copy_options::overwrite_existing);

    // 写 meta.json
    {
        nlohmann::json meta;
        meta["strategy_id"] = strategyName;
        meta["created_at"] = ToString(now, "%Y-%m-%dT%H:%M:%S");
        meta["source"] = "experiment";
        meta["label"] = labelCfg;
        meta["objective"] = objective;
        meta["num_class"] = numClass;
        meta["test_ratio"] = testRatio;
        meta["params"] = xgbParams;
        meta["date_range"] = dateRangeCfg;
        meta["features"] = featureNames;
        if (trainResult.contains("eval_metrics")) meta["eval_metrics"] = trainResult["eval_metrics"];
        if (trainResult.contains("n_train")) meta["n_train"] = trainResult["n_train"];
        if (trainResult.contains("n_test")) meta["n_test"] = trainResult["n_test"];
        meta["n_features"] = featureNames.size();

        String metaPath = expDir + "/" + persistName + ".meta.json";
        std::ofstream ofs(metaPath);
        if (ofs.is_open()) ofs << meta.dump(2);
    }

    // ============== 8. 加载模型到内存供 SHAP ==============
    BoosterHandle booster = nullptr;
    if (XGBoosterCreate(nullptr, 0, &booster) == 0 && booster) {
        if (XGBoosterLoadModel(booster, modelPath.c_str()) != 0) {
            XGBoosterFree(booster);
            booster = nullptr;
        }
    }

    Vector<Vector<double>> Xtest;
    if (trainResult.contains("X_test") && trainResult["X_test"].is_array()) {
        for (auto& row : trainResult["X_test"]) {
            Vector<double> rowVec;
            for (auto& v : row) {
                rowVec.push_back(v.get<double>());
            }
            Xtest.push_back(std::move(rowVec));
        }
    }

    uint64_t modelId = 0;
    if (booster) {
        // 使用 Python 训练时实际的特征列表（排除了 label 列），而非 C++ 上游节点的全部输出
        Vector<String> actualFeatures;
        if (trainResult.contains("features") && trainResult["features"].is_array()) {
            for (auto& f : trainResult["features"]) {
                actualFeatures.push_back(f.get<String>());
            }
        } else {
            actualFeatures = featureNames;
        }
        modelId = registerModel(booster, actualFeatures, Xtest);
        trainResult["model_id"] = modelId;
    }

    trainResult.erase("X_test");
    trainResult["model_path"] = persistPath;
    std::remove(csvPath.c_str());
    std::remove(modelPath.c_str());

    res.set_content(trainResult.dump(), "application/json");
}

void XGBoostHandler::handleShap(const nlohmann::json& params, httplib::Response& res) {
    uint64_t modelId = 0;
    if (params.contains("model_id")) {
        try { modelId = params["model_id"].get<uint64_t>(); }
        catch (...) {
            res.status = 400;
            res.set_content(R"({"message":"invalid model_id"})", "application/json");
            return;
        }
    } else {
        res.status = 400;
        res.set_content(R"({"message":"missing model_id"})", "application/json");
        return;
    }

    auto* model = getModel(modelId);
    if (!model || !model->booster) {
        res.status = 404;
        res.set_content(R"({"message":"model not found"})", "application/json");
        return;
    }

    if (model->X_test.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"model has no test data"})", "application/json");
        return;
    }

    size_t n_features = model->features.size();
    size_t n_samples = model->X_test.size();

    std::vector<float> flat(n_samples * n_features);
    for (size_t i = 0; i < n_samples; ++i) {
        for (size_t j = 0; j < n_features; ++j) {
            double v = (j < model->X_test[i].size()) ? model->X_test[i][j] : 0.0;
            flat[i * n_features + j] = static_cast<float>(v);
        }
    }

    DMatrixHandle dmat = nullptr;
    auto ret = XGDMatrixCreateFromMat(flat.data(),
                                       static_cast<bst_ulong>(n_samples),
                                       static_cast<bst_ulong>(n_features),
                                       0.0f,
                                       &dmat);
    if (ret != 0 || !dmat) {
        res.status = 500;
        res.set_content(R"({"message":"DMatrix create failed"})", "application/json");
        return;
    }

    bst_ulong out_dim = 0;
    const float* out_data = nullptr;
    size_t n_out;

    // 调试日志：调用前
    INFO("[XGBoost SHAP] Before predict: booster={}, dmat={}, n_samples={}, n_features={}", 
         (void*)model->booster, (void*)dmat, n_samples, n_features);

#if XGBOOST_VER_MAJOR >= 2
    // XGBoost >= 2.x: 使用 config JSON 字符串
    // PredictionType: kValue=0, kMargin=1, kContribution=2 (SHAP), kApproxContribution=3, kInteraction=4
    const char* predict_config = R"({"type": 2, "training": false, "iteration_begin": 0, "iteration_end": 0, "strict_shape": true})";
    bst_ulong const* out_shape = nullptr;
    
    ret = XGBoosterPredictFromDMatrix(model->booster, dmat,
                                       predict_config,
                                       &out_shape, &out_dim, &out_data);
    
    // 调试日志：调用后
    INFO("[XGBoost SHAP] After predict: ret={}, out_dim={}, out_data={}, out_shape={}",
         ret, out_dim, (void*)out_data, (void*)out_shape);
    if (ret != 0 || !out_data) {
        FATAL("[XGBoost SHAP] XGBoost error: {}", XGBGetLastError());
    }
    if (out_shape && out_dim > 0) {
        String shapeStr = "out_shape=[";
        for (bst_ulong d = 0; d < out_dim; ++d) {
            if (d > 0) shapeStr += ", ";
            shapeStr += std::to_string(out_shape[d]);
        }
        shapeStr += "]";
        INFO("[XGBoost SHAP] {}", shapeStr);
    }
    
    // out_shape[0] = 总元素数 = n_samples × out_dim；n_samples 沿用预设值
    n_out = n_samples * out_dim;
#else
    // XGBoost < 2.x: 旧版 (option, ntree_limit, training) 参数
    bst_ulong out_n = 0;
    float* out_data_mut = nullptr;
    ret = XGBoosterPredictFromDMatrix(model->booster, dmat,
                                       0, 0, 1,  // XGBOOST_OUTPUT_CONTRIBUTION
                                       &out_n, &out_dim, &out_data_mut);
    out_data = out_data_mut;
    n_samples = out_n;
    n_out = n_samples * out_dim;
#endif

    XGDMatrixFree(dmat);

    if (ret != 0 || !out_data) {
        res.status = 500;
        res.set_content(R"({"message":"SHAP prediction failed"})", "application/json");
        return;
    }

    nlohmann::json shapList = nlohmann::json::array();
    nlohmann::json baseList = nlohmann::json::array();

    // 多分类 SHAP 输出为 3D: (n_samples, n_classes, n_features+1)
    // 二分类 SHAP 输出为 2D: (n_samples, n_features+1)
    // out_dim 是维度数（3D=3, 2D=2），不是每样本元素数
    // 需要从 out_shape 获取实际形状
    size_t stridePerSample = 0;
    size_t nClasses = 1;
    bool isMultiClass = false;

    if (out_shape && out_dim >= 2) {
        if (out_dim == 3) {
            // 3D: (n_samples, n_classes, n_features+1)
            nClasses = out_shape[1];
            stridePerSample = nClasses * (n_features + 1);
            isMultiClass = true;
        } else {
            // 2D: (n_samples, n_features+1)
            stridePerSample = n_features + 1;
        }
    } else {
        // fallback: 假设 2D
        stridePerSample = n_features + 1;
    }

    for (size_t i = 0; i < n_samples; ++i) {
        nlohmann::json featsArr = nlohmann::json::array();
        if (isMultiClass) {
            // 多分类：每个特征取所有类别 SHAP 的平均值
            for (size_t j = 0; j < n_features; ++j) {
                double sum = 0;
                for (size_t c = 0; c < nClasses; ++c) {
                    sum += out_data[i * stridePerSample + c * (n_features + 1) + j];
                }
                featsArr.push_back(sum / nClasses);
            }
            // base_value 取所有类别的平均
            double baseSum = 0;
            for (size_t c = 0; c < nClasses; ++c) {
                baseSum += out_data[i * stridePerSample + c * (n_features + 1) + n_features];
            }
            baseList.push_back(baseSum / nClasses);
        } else {
            // 二分类：直接取前 n_features 个值
            for (size_t j = 0; j < n_features; ++j) {
                featsArr.push_back(out_data[i * stridePerSample + j]);
            }
            // base_value 是最后一个值
            baseList.push_back(out_data[i * stridePerSample + n_features]);
        }
        shapList.push_back(featsArr);
    }

    nlohmann::json featuresArr = nlohmann::json::array();
    for (auto& f : model->features) featuresArr.push_back(f);

    nlohmann::json resp = {
        {"model_id", modelId},
        {"features", featuresArr},
        {"shap", shapList},
        {"base_value", baseList},
        {"n_samples", n_samples},
    };
    res.set_content(resp.dump(), "application/json");
}

void XGBoostHandler::handlePublish(const nlohmann::json& params, httplib::Response& res) {
    String modelPath = params.value("model_path", "");
    if (modelPath.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing 'model_path'"})", "application/json");
        return;
    }

    if (!std::filesystem::exists(modelPath)) {
        res.status = 404;
        res.set_content(R"({"message":"model file not found"})", "application/json");
        return;
    }

    // 从文件名提取 strategy_id 和 timestamp
    std::filesystem::path p(modelPath);
    String stem = p.stem().string();  // e.g. "strategy_xgb_20260727_153012"
    // 去掉末尾的时间戳 _YYYYMMDD_HHMMSS 得到 strategy_id
    String strategyId = stem;
    auto lastUnderscore = stem.rfind('_');
    if (lastUnderscore != String::npos) {
        auto prevUnderscore = stem.rfind('_', lastUnderscore - 1);
        if (prevUnderscore != String::npos) {
            // 检查是否符合 _YYYYMMDD_HHMMSS 格式
            String suffix = stem.substr(prevUnderscore + 1);
            if (suffix.size() == 15 && suffix[8] == '_') {
                strategyId = stem.substr(0, prevUnderscore);
            }
        }
    }

    String dbPath = _server->GetConfig().GetDatabasePath();
    String prodDir = dbPath + "/models/production";
    std::filesystem::create_directories(prodDir);

    String prodModelPath = prodDir + "/" + strategyId + ".json";
    String prodMetaPath = prodDir + "/" + strategyId + ".meta.json";

    // 复制模型文件
    std::filesystem::copy(modelPath, prodModelPath, std::filesystem::copy_options::overwrite_existing);

    // 读取实验 meta 并写入 production meta（附加发布来源信息）
    String expMetaPath = modelPath;
    auto dotPos = expMetaPath.rfind('.');
    if (dotPos != String::npos) {
        expMetaPath = expMetaPath.substr(0, dotPos) + ".meta.json";
    }

    nlohmann::json prodMeta;
    if (std::filesystem::exists(expMetaPath)) {
        std::ifstream ifs(expMetaPath);
        if (ifs.is_open()) {
            try {
                prodMeta = nlohmann::json::parse(ifs);
            } catch (...) {}
        }
    }
    prodMeta["source"] = "experiment";
    prodMeta["published_from"] = stem;
    prodMeta["published_at"] = ToString(Now(), "%Y-%m-%dT%H:%M:%S");

    std::ofstream ofs(prodMetaPath);
    if (ofs.is_open()) ofs << prodMeta.dump(2);

    nlohmann::json resp = {
        {"message", "published"},
        {"production_path", prodModelPath},
        {"strategy_id", strategyId},
    };
    res.set_content(resp.dump(), "application/json");
}

void XGBoostHandler::handleList(httplib::Response& res) {
    String dbPath = _server->GetConfig().GetDatabasePath();
    String expDir = dbPath + "/models/experiments";
    String prodDir = dbPath + "/models/production";

    nlohmann::json experiments = nlohmann::json::array();

    if (std::filesystem::exists(expDir) && std::filesystem::is_directory(expDir)) {
        // 收集所有 .json（非 .meta.json）文件
        Vector<std::filesystem::path> modelFiles;
        for (auto& entry : std::filesystem::directory_iterator(expDir)) {
            String fname = entry.path().filename().string();
            if (fname.size() > 5 && fname.substr(fname.size() - 5) == ".json"
                && fname.find(".meta.json") == String::npos) {
                modelFiles.push_back(entry.path());
            }
        }
        // 按文件名降序（最新在前）
        std::sort(modelFiles.begin(), modelFiles.end(), std::greater<>());

        for (auto& fp : modelFiles) {
            nlohmann::json item;
            item["path"] = fp.string();
            item["filename"] = fp.filename().string();

            String metaPath = fp.string();
            auto dotPos = metaPath.rfind('.');
            if (dotPos != String::npos) {
                metaPath = metaPath.substr(0, dotPos) + ".meta.json";
            }
            if (std::filesystem::exists(metaPath)) {
                std::ifstream ifs(metaPath);
                if (ifs.is_open()) {
                    try {
                        item["meta"] = nlohmann::json::parse(ifs);
                    } catch (...) {
                        item["meta"] = nullptr;
                    }
                }
            }
            experiments.push_back(item);
        }
    }

    nlohmann::json production = nullptr;
    if (std::filesystem::exists(prodDir) && std::filesystem::is_directory(prodDir)) {
        for (auto& entry : std::filesystem::directory_iterator(prodDir)) {
            String fname = entry.path().filename().string();
            if (fname.size() > 5 && fname.substr(fname.size() - 5) == ".json"
                && fname.find(".meta.json") == String::npos) {
                nlohmann::json item;
                item["path"] = entry.path().string();
                item["filename"] = fname;

                String metaPath = entry.path().string();
                auto dotPos = metaPath.rfind('.');
                if (dotPos != String::npos) {
                    metaPath = metaPath.substr(0, dotPos) + ".meta.json";
                }
                if (std::filesystem::exists(metaPath)) {
                    std::ifstream ifs(metaPath);
                    if (ifs.is_open()) {
                        try {
                            item["meta"] = nlohmann::json::parse(ifs);
                        } catch (...) {
                            item["meta"] = nullptr;
                        }
                    }
                }
                production = item;
                break;
            }
        }
    }

    nlohmann::json resp = {
        {"experiments", experiments},
        {"production", production},
    };
    res.set_content(resp.dump(), "application/json");
}

void XGBoostHandler::handleDelete(uint64_t modelId, httplib::Response& res) {
    if (deleteModel(modelId)) {
        res.set_content(R"({"message":"deleted"})", "application/json");
    } else {
        res.status = 404;
        res.set_content(R"({"message":"model not found"})", "application/json");
    }
}

void XGBoostHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params;
    try {
        params = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid JSON"})", "application/json");
        return;
    }

    String action = params.value("action", "");
    if (action == "train") {
        handleTrain(params, res);
    } else if (action == "shap") {
        handleShap(params, res);
    } else if (action == "publish") {
        handlePublish(params, res);
    } else {
        res.status = 400;
        res.set_content(
            "{\"message\":\"missing or invalid 'action' (train|shap|publish)\"}",
            "application/json");
        return;
    }
}

void XGBoostHandler::get(const httplib::Request& req, httplib::Response& res) {
    String action = req.get_param_value("action");
    if (action == "list") {
        handleList(res);
    } else if (action == "shap") {
        // 从 query params 构造 params json
        nlohmann::json params;
        String modelIdStr = req.get_param_value("model_id");
        if (modelIdStr.empty()) {
            res.status = 400;
            res.set_content(R"({"message":"missing model_id"})", "application/json");
            return;
        }
        try {
            params["model_id"] = std::stoull(modelIdStr);
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"message":"invalid model_id"})", "application/json");
            return;
        }
        handleShap(params, res);
    } else {
        res.status = 400;
        res.set_content(
            "{\"message\":\"missing or invalid 'action' (list|shap)\"}",
            "application/json");
        return;
    }
}

void XGBoostHandler::del(const httplib::Request& req, httplib::Response& res) {
    String modelIdStr = req.get_param_value("model_id");
    if (modelIdStr.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing model_id"})", "application/json");
        return;
    }
    uint64_t modelId = 0;
    try {
        modelId = std::stoull(modelIdStr);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"invalid model_id"})", "application/json");
        return;
    }
    handleDelete(modelId, res);
}
