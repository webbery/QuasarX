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
#include "Util/string_algorithm.h"
#include "server.h"
#include "std_header.h"
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <unistd.h>

extern "C" {
#include <xgboost/c_api.h>
}

namespace {

String makeTempPath(const String& prefix, const String& ext) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    return fmt::format("/tmp/{}_{}_{}.{}", prefix, getpid(), gen() & 0xFFFFFF, ext);
}

bool writeCsv(const String& path, const Map<String, Vector<double>>& data) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
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

namespace {

// ============ 各个 action 的处理函数 ============

void handleTrain(XGBoostHandler* self, const nlohmann::json& params, httplib::Response& res, Server* server) {
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
    double testRatio = params.value("test_ratio", 0.2);

    String labelSource = labelCfg.value("source", "");
    int labelPeriod = labelCfg.value("period", 5);
    String labelType = labelCfg.value("type", "classification");
    double threshold = labelCfg.value("threshold", 0.0);
    String objective = params.value("objective", labelType == "classification" ? "binary:logistic" : "reg:squarederror");
    int numClass = params.value("num_class", 2);

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
        fullGraph = parse_strategy_script_v2(script, server);
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
    auto* exchangeMgr = server->GetExchangeManager();
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
    auto* flowSubsystem = server->GetStrategySystem()->GetFlowSubsystem();
    double initialCapital = 100000.0;
    String tmpStrategyName = strategyName + "_train";

    Map<String, Vector<double>> collected;
    bool collectOk = flowSubsystem->RunTrainingCollect(
        tmpStrategyName, upstreamSubgraph, requiredSources,
        symbols, initialCapital, collected);

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

    writeCsv(csvPath, collected);

    auto pyEnv = PythonEnv::fromConfig(server->GetConfig().GetRawConfig());
    auto interpreter = pyEnv.resolve(params.value("py_env", ""));

    std::vector<std::string> args = {
        "--data", csvPath,
        "--label-source", labelSource,
        "--label-period", std::to_string(labelPeriod),
        "--label-type", labelType,
        "--threshold", std::to_string(threshold),
        "--objective", objective,
        "--num-class", std::to_string(numClass),
        "--model-output", modelPath,
        "--params", xgbParams.dump(),
        "--test-ratio", std::to_string(testRatio),
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
        }
    }

    for (auto n : fullGraph) delete n;

    if (resultLine.empty()) {
        res.status = 500;
        res.set_content(R"({"message":"训练脚本未输出 result，请查看日志"})", "application/json");
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

    // ============== 7. 加载模型到内存供 SHAP ==============
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
        modelId = self->registerModel(booster, featureNames, Xtest);
        trainResult["model_id"] = modelId;
    }

    trainResult.erase("X_test");
    std::remove(csvPath.c_str());
    std::remove(modelPath.c_str());

    res.set_content(trainResult.dump(), "application/json");
}

void handleShap(XGBoostHandler* self, const nlohmann::json& params, httplib::Response& res) {
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

    auto* model = self->getModel(modelId);
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

    bst_ulong out_n = 0;
    bst_ulong out_dim = 0;
    float* out_data = nullptr;

    ret = XGBoosterPredictFromDMatrix(model->booster, dmat,
                                       0, 0, 1,  // XGBOOST_OUTPUT_CONTRIBUTION
                                       &out_n, &out_dim, &out_data);
    XGDMatrixFree(dmat);

    if (ret != 0 || !out_data) {
        res.status = 500;
        res.set_content(R"({"message":"SHAP prediction failed"})", "application/json");
        return;
    }

    nlohmann::json shapList = nlohmann::json::array();
    nlohmann::json baseList = nlohmann::json::array();
    for (size_t i = 0; i < out_n; ++i) {
        nlohmann::json featsArr = nlohmann::json::array();
        for (size_t j = 0; j < n_features; ++j) {
            featsArr.push_back(out_data[i * out_dim + j]);
        }
        shapList.push_back(featsArr);
        baseList.push_back(out_data[i * out_dim + (out_dim - 1)]);
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

void handleDelete(XGBoostHandler* self, const nlohmann::json& params, httplib::Response& res) {
    uint64_t id = 0;
    if (params.contains("model_id")) {
        try { id = params["model_id"].get<uint64_t>(); }
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
    if (self->deleteModel(id)) {
        res.set_content(R"({"message":"deleted"})", "application/json");
    } else {
        res.status = 404;
        res.set_content(R"({"message":"model not found"})", "application/json");
    }
}

}  // namespace

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
        handleTrain(this, params, res, _server);
    } else if (action == "shap") {
        handleShap(this, params, res);
    } else if (action == "delete") {
        handleDelete(this, params, res);
    } else {
        res.status = 400;
        res.set_content(R"({"message":"missing or invalid 'action' (train|shap|delete)"})", "application/json");
        return;
    }
}
