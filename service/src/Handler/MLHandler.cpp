#include "Handler/MLHandler.h"
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
#include <numeric>
#include <cmath>
#include <limits>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

extern "C" {
#include <xgboost/c_api.h>
}

// 静态成员定义
std::shared_ptr<TrainSession> MLHandler::s_activeSession = nullptr;
std::mutex MLHandler::s_sessionMtx;

namespace {

#ifdef _WIN32
inline int QS_GETPID() { return _getpid(); }
#else
inline int QS_GETPID() { return getpid(); }
#endif

String makeTempPath(const String& prefix, const String& ext) {
    namespace fs = std::filesystem;
    std::random_device rd;
    std::mt19937_64 gen(rd());
    auto filename = fmt::format("{}_{}_{}.{}", prefix, QS_GETPID(), gen() & 0xFFFFFF, ext);
    return (fs::temp_directory_path() / filename).string();
}

bool writeCsv(const String& path, const Map<String, Vector<double>>& data, const Vector<String>& dates,
              const Vector<String>* order = nullptr) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    // 列顺序：指定 order 时（如 XGBoostNode 推理侧特征顺序）优先，剩余列（如 label 列）保持原顺序
    Vector<String> cols;
    if (order) {
        for (auto& k : *order) {
            if (data.count(k)) cols.push_back(k);
        }
        for (auto& [k, _] : data) {
            if (std::find(cols.begin(), cols.end(), k) == cols.end()) cols.push_back(k);
        }
    } else {
        for (auto& [k, _] : data) cols.push_back(k);
    }
    // 写表头：date 列 + 数据列
    bool hasDates = !dates.empty();
    if (hasDates) ofs << "date,";
    bool first = true;
    for (auto& k : cols) {
        if (!first) ofs << ",";
        ofs << k;
        first = false;
    }
    ofs << "\n";
    size_t rows = 0;
    for (auto& k : cols) {
        auto itr = data.find(k);
        if (itr != data.end() && !itr->second.empty()) { rows = itr->second.size(); break; }
    }
    for (size_t i = 0; i < rows; ++i) {
        first = true;
        if (hasDates) {
            ofs << (i < dates.size() ? dates[i] : "");
            ofs << ",";
        }
        for (auto& k : cols) {
            if (!first) ofs << ",";
            const auto& v = data.at(k);
            double val = (i < v.size()) ? v[i] : 0.0;
            if (val != val) ofs << "";
            else ofs << val;
            first = false;
        }
        ofs << "\n";
    }
    return true;
}

// 过滤收集的数据：只保留特征列 + label source (train) + matrix 模式 close 列
// 返回 {filtered, droppedKeys}
std::pair<Map<String, Vector<double>>, Vector<String>>
filterCollectedData(const Map<String, Vector<double>>& collected,
                    const Vector<String>& featureNames,
                    const String& labelSource,
                    const String& labelShape) {
    Map<String, Vector<double>> filtered;
    Vector<String> droppedKeys;

    // 1. 按特征名过滤（_featureNames 是短名如 MA(5)，collected keys 是全名如 sz.800001.MA(5)）
    //    排除 org_* 原始价列（QuoteInputNode 为每个价格字段同时输出调整后和原始值，
    //    org_close 不应作为模型特征；后缀匹配 "close" 会误匹配 "org_close"）
    for (auto& [k, v] : collected) {
        // 检查最后一个 . 之后是否以 org 开头（如 sz.800001.org_close/org_open/org_high/org_low）
        auto lastDot = k.rfind('.');
        if (lastDot != String::npos && lastDot + 4 <= k.size() &&
            k.compare(lastDot + 1, 3, "org") == 0) {
            droppedKeys.push_back(k);
            continue;
        }
        bool matched = false;
        for (auto& feat : featureNames) {
            if (k == feat || (k.size() > feat.size() &&
                k.substr(k.size() - feat.size()) == feat &&
                k[k.size() - feat.size() - 1] == '.')) {
                filtered[k] = v;
                matched = true;
                break;
            }
        }
        if (!matched) droppedKeys.push_back(k);
    }

    // 2. 保留 label 列（train 路径需要，collect 路径 labelSource 为空）
    if (!labelSource.empty() && collected.count(labelSource) && !filtered.count(labelSource)) {
        filtered[labelSource] = collected.at(labelSource);
    }

    // 3. matrix 模式：保留每个标的的 close 列（Python 脚本用于计算 per-symbol 标签）
    if (labelShape == "matrix") {
        for (auto& [k, v] : collected) {
            if (!filtered.count(k)) {
                // 只匹配 {symbol}.close，排除 org_close/high/low 等
                if (k.size() > 6 && k.substr(k.size() - 6) == ".close") {
                    filtered[k] = v;
                }
            }
        }
    }

    return {std::move(filtered), std::move(droppedKeys)};
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

uint64_t MLHandler::registerModel(ModelType type, BoosterHandle booster, Vector<String> features, Eigen::MatrixXd x_test) {
    std::lock_guard<std::mutex> lock(_mtx);
    uint64_t id = _nextId.fetch_add(1);
    _cache[id]._modelType = type;
    _cache[id]._booster = booster;
    _cache[id]._features = std::move(features);
    _cache[id]._x_test = std::move(x_test);
    return id;
}

CachedMLModel* MLHandler::getModel(uint64_t id) {
    std::lock_guard<std::mutex> lock(_mtx);
    auto itr = _cache.find(id);
    return itr == _cache.end() ? nullptr : &itr->second;
}

void MLHandler::collectXGBoostFeatures(
    const List<QNode*>& upstreamSubgraph,
    Vector<String>& outFeatureNames)
{
    XGBoostNode* xgb = nullptr;
    for (auto n : upstreamSubgraph) {
        if (auto* x = dynamic_cast<XGBoostNode*>(n)) { xgb = x; break; }
    }
    if (!xgb) {
        WARN("[MLTrain] XGBoostNode not found in upstream subgraph");
        return;
    }

    // 按上游节点 id 排序，保证特征顺序稳定（与推理侧一致）
    Set<uint32_t> visited;
    Vector<std::pair<uint32_t, QNode*>> sortedIns;
    for (auto& [handle, nodePtr] : xgb->ins()) {
        if (nodePtr && visited.insert(nodePtr->id()).second)
            sortedIns.push_back({nodePtr->id(), nodePtr});
    }
    std::sort(sortedIns.begin(), sortedIns.end());

    const auto& featureKeys = xgb->featureKeys();

    if (featureKeys.empty()) {
        // features 参数未指定 → 收集全部上游输出（兼容旧行为）
        for (auto& [id, nodePtr] : sortedIns) {
            auto elements = nodePtr->out_elements();
            Vector<String> elemKeys;
            for (auto& [k, _] : elements) {
                outFeatureNames.push_back(k);
                elemKeys.push_back(k);
            }
            INFO("[MLTrain] XGBoost upstream node#{} contributes {} keys: [{}]",
                 id, elemKeys.size(), fmt::join(elemKeys, ", "));
        }
    } else {
        // features 参数已指定 → 只收集匹配的特征（训练与推理对齐）
        // 从上游 QuoteInputNode 收集 symbol 字符串，把 featSet 内每个 key 剥前缀成 shortName，
        // 与循环里 out_elements 的 shortName 在同一比较空间匹配（XGBoostNode Init 阶段
        // _feature_keys 可能已是带前缀的 fullKey，必须先归一化）
        Set<String> symbolStrs;
        for (auto n : upstreamSubgraph) {
            if (auto* qi = dynamic_cast<QuoteInputNode*>(n)) {
                for (auto s : qi->GetSymbols())
                    symbolStrs.insert(get_symbol(s));
            }
        }
        auto stripPrefix = [&symbolStrs](const String& k) -> String {
            for (auto& sym : symbolStrs) {
                String prefix = sym + ".";
                if (k.size() > prefix.size() &&
                    k.compare(0, prefix.size(), prefix) == 0) {
                    return k.substr(prefix.size());
                }
            }
            return k;
        };
        Set<String> featSet;
        for (auto& k : featureKeys) featSet.insert(stripPrefix(k));

        for (auto& [id, nodePtr] : sortedIns) {
            auto elements = nodePtr->out_elements();
            Vector<String> matched;
            for (auto& [fullKey, _] : elements) {
                // 从全名提取短名：sh.600176.emd.nimf_0 → emd.nimf_0
                String shortName = stripPrefix(fullKey);
                if (featSet.count(shortName)) {
                    outFeatureNames.push_back(fullKey);
                    matched.push_back(shortName);
                }
            }
            INFO("[MLTrain] XGBoost upstream node#{} matched {}/{} keys: [{}]",
                 id, matched.size(), elements.size(), fmt::join(matched, ", "));
        }
    }
    INFO("[MLTrain] Total features collected: {} entries: [{}]",
         outFeatureNames.size(), fmt::join(outFeatureNames, ", "));
}

bool MLHandler::deleteModel(uint64_t id) {
    std::lock_guard<std::mutex> lock(_mtx);
    auto itr = _cache.find(id);
    if (itr == _cache.end()) return false;
    itr->second.clear();
    _cache.erase(itr);
    return true;
}

// ============ 各个 action 的处理函数 ============

// 训练中间状态（不在 TrainSession 中，仅训练/收集线程使用）
struct TrainState {
    List<QNode*> _fullGraph;
    Set<QNode*> _upstreamSet;
    List<QNode*> _upstreamSubgraph;
    List<QNode*> _collectGraph;  // _upstreamSubgraph 排除 XGBoostNode，用于 TrainingCollect 执行
    Vector<String> _featureNames;
    String _tmpStrategyName;
    String _csvPath, _modelPath;
};

void MLHandler::handleOptimize(const nlohmann::json& params, httplib::Response& res) {
    // 幂等检查：与 train 共用 s_activeSession
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        if (s_activeSession && !s_activeSession->_done) {
            nlohmann::json resp;
            resp["session_id"] = s_activeSession->_sessionId;
            resp["status"] = "running";
            res.set_content(resp.dump(), "application/json");
            return;
        }
    }

    // 解析请求：feature_cache + fast_backtest_strategy 为必填
    String featureCache = params.value("feature_cache", "");
    String strFastStrategy = params.value("fast_backtest_strategy", "");
    if (featureCache.empty() || strFastStrategy.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing 'feature_cache' or 'fast_backtest_strategy'"})", "application/json");
        return;
    }

    nlohmann::json fastStrategy;
    try { fastStrategy = nlohmann::json::parse(strFastStrategy); }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"invalid 'fast_backtest_strategy' JSON"})", "application/json");
        return;
    }

    nlohmann::json labelCfg = params.value("label", nlohmann::json::object());
    String labelSource = labelCfg.value("source", "");
    int labelPeriod = labelCfg.value("period", 5);
    String labelType = labelCfg.value("type", "classification");
    String labelShape = labelCfg.value("shape", "matrix");
    double volK = labelCfg.value("vol_k", 0.5);
    String objective = params.value("objective", "multi:softprob");
    int numClass = params.value("num_class", 3);
    double testRatio = params.value("test_ratio", 0.2);
    double valRatio = params.value("val_ratio", 0.15);

    int nTrials = params.value("n_trials", 20);
    String paramDomains = params.value("param_domains", "{}");
    String optimizeMetric = params.value("optimize_metric", "sharpe");
    String startDate = params.value("start_date", "");
    String endDate = params.value("end_date", "");
    String frequency = params.value("frequency", "1d");

    auto session = std::make_shared<TrainSession>();
    session->_sessionId = fmt::format("xgb_opt_{}", std::chrono::steady_clock::now().time_since_epoch().count());
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        s_activeSession = session;
    }

    auto sendSSE = [session](const String& type, const nlohmann::json& data) {
        session->pushEvent(type, data);
    };

    // 后台线程：调 xgboost_optimize.py（前端先调 collect 拿到 csv_path 作为 feature_cache 传入）
    std::thread([this, params, fastStrategy, session, sendSSE, labelSource, labelPeriod,
                 labelType, labelShape, volK, objective, numClass, testRatio, valRatio, nTrials,
                 paramDomains, optimizeMetric, startDate, endDate, frequency, featureCache]() {
        String strategyPath;
        try {
            strategyPath = makeTempPath("xgb_opt_strategy", "json");
            std::ofstream sfs(strategyPath);
            sfs << fastStrategy.dump();
            sfs.close();

            auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
            auto interpreter = pyEnv.resolve(params.value("py_env", ""));

            // base-url 用 localhost 直连（同进程）；auth_token 透传
            String baseUrl = "http://localhost:19107";
            String authToken = params.value("auth_token", "");

            std::vector<std::string> args = {
                "--data", featureCache,
                "--feature-cache", featureCache,
                "--fast-backtest-strategy", strategyPath,
                "--base-url", baseUrl,
                "--label-source", labelSource,
                "--label-period", std::to_string(labelPeriod),
                "--label-type", labelType,
                "--label-shape", labelShape,
                "--vol-k", std::to_string(volK),
                "--objective", objective,
                "--num-class", std::to_string(numClass),
                "--test-ratio", std::to_string(testRatio),
                "--val-ratio", std::to_string(valRatio),
                "--n-trials", std::to_string(nTrials),
                "--optimize-metric", optimizeMetric,
                "--param-domains", paramDomains,
                "--frequency", frequency,
            };
            if (!startDate.empty()) args.insert(args.end(), {"--start-date", startDate});
            if (!endDate.empty()) args.insert(args.end(), {"--end-date", endDate});
            if (!authToken.empty()) args.insert(args.end(), {"--auth-token", authToken});

            sendSSE("step", {{"step","optimize"},{"status","start"},
                              {"script","tools/xgboost_optimize.py"},{"n_trials",nTrials},
                              {"metric",optimizeMetric}});

            PythonRunner runner;
            INFO("[MLOptimize] Starting Python: interpreter='{}', n_trials={}", interpreter, nTrials);
            if (!runner.start("tools/xgboost_optimize.py", args, interpreter)) {
                sendSSE("error", {{"step","optimize"},{"msg","failed to start xgboost_optimize.py"}});
                std::lock_guard<std::mutex> lk(session->_mtx);
                session->_hasError = true;
                session->_result = {{"error","failed to start optimization script"}};
                session->_done = true;
                std::remove(strategyPath.c_str());
                return;
            }

            String resultLine, stderrLines;
            PythonOutput out;
            while (runner.readLine(out, 60000)) {
                if (out.type == PythonOutput::DONE) break;
                if (out.type == PythonOutput::STDOUT) {
                    if (out.line.find("\"type\":\"result\"") != std::string::npos) {
                        resultLine = out.line;
                    } else if (out.line.find("\"type\":\"error\"") != std::string::npos) {
                        sendSSE("warning", {{"line", out.line}});
                        if (stderrLines.size() < 1000) stderrLines += out.line + "\n";
                    } else {
                        try {
                            auto j = nlohmann::json::parse(out.line);
                            String t = j.value("type", "info");
                            j.erase("type");
                            sendSSE(t, j);
                        } catch (...) {
                            sendSSE("log", {{"line", out.line}});
                        }
                    }
                } else if (out.type == PythonOutput::STDERR) {
                    sendSSE("warning", {{"line", out.line}});
                    if (stderrLines.size() < 1000) stderrLines += out.line + "\n";
                }
            }

            std::lock_guard<std::mutex> lk(session->_mtx);
            if (resultLine.empty()) {
                String msg = stderrLines.empty() ? "optimize script 未输出 result" : stderrLines.substr(0, 500);
                while (!msg.empty() && msg.back() == '\n') msg.pop_back();
                session->_hasError = true;
                session->_result = {{"error", String("优化失败: ") + msg}};
            } else {
                try { session->_result = nlohmann::json::parse(resultLine); }
                catch (...) {
                    session->_hasError = true;
                    session->_result = {{"error","优化结果解析失败"}};
                }
            }
            session->_done = true;
            std::remove(strategyPath.c_str());
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(session->_mtx);
            session->_hasError = true;
            session->_result = {{"error", String("unhandled exception: ") + e.what()}};
            session->_done = true;
            if (!strategyPath.empty()) std::remove(strategyPath.c_str());
        } catch (...) {
            std::lock_guard<std::mutex> lk(session->_mtx);
            session->_hasError = true;
            session->_result = {{"error","unknown exception during optimization"}};
            session->_done = true;
            if (!strategyPath.empty()) std::remove(strategyPath.c_str());
        }
    }).detach();

    nlohmann::json resp;
    resp["session_id"] = session->_sessionId;
    resp["status"] = "running";
    resp["n_trials"] = nTrials;
    resp["optimize_metric"] = optimizeMetric;
    res.set_content(resp.dump(), "application/json");
}

void MLHandler::handleTrain(const nlohmann::json& params, httplib::Response& res) {
    // ============== 幂等检查：如果已有活跃训练，返回其 session_id ==============
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        if (s_activeSession && !s_activeSession->_done) {
            nlohmann::json resp;
            resp["session_id"] = s_activeSession->_sessionId;
            resp["status"] = "running";
            res.set_content(resp.dump(), "application/json");
            INFO("[MLTrain] Returning existing session: {}", s_activeSession->_sessionId);
            return;
        }
    }

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
        INFO("[MLTrain] Parsed script keys: {}", script.dump().substr(0, 200));
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid strategy script JSON"})", "application/json");
        return;
    }

    nlohmann::json labelCfg = params.value("label", nlohmann::json::object());
    nlohmann::json xgbParams = params.value("params", nlohmann::json::object());
    nlohmann::json dateRangeCfg = params.value("date_range", nlohmann::json::object());
    double testRatio = params.value("test_ratio", 0.2);
    double valRatio = params.value("val_ratio", 0.15);

    String labelSource = labelCfg.value("source", "");
    int labelPeriod = labelCfg.value("period", 5);
    String labelType = labelCfg.value("type", "classification");
    String labelShape = labelCfg.value("shape", "matrix");
    double volK = labelCfg.value("vol_k", 0.5);
    String objective = params.value("objective", labelType == "classification" ? "multi:softprob" : "reg:squarederror");
    int numClass = params.value("num_class", 3);

    // 日期范围和频率（与标签分析一致）
    String startDate = dateRangeCfg.value("start", "");
    String endDate = dateRangeCfg.value("end", "");
    String frequency = dateRangeCfg.value("frequency", "1d");

    ModelType modelType = stringToModelType(params.value("model_type", "xgboost"));
    if (modelType != ModelType::XGBoost) {
        res.status = 400;
        res.set_content(R"({"message":"model_type ')" + modelTypeToString(modelType) + R"(' not implemented yet"})", "application/json");
        return;
    }

    String strategyName = script.value("id", "xgboost_train");
    if (labelSource.empty() && labelShape != "matrix") {
        res.status = 400;
        res.set_content(R"xx({"message":"missing label.source (required for vector mode)"})xx", "application/json");
        return;
    }

    // ============== 创建训练会话 ==============
    auto state = std::make_shared<TrainState>();

    auto session = std::make_shared<TrainSession>();
    session->_sessionId = "xgb_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        s_activeSession = session;
    }

    // sendSSE: 推送事件到会话（线程安全，支持重连回放）
    auto sendSSE = [session](const String& type, const nlohmann::json& data) {
        session->pushEvent(type, data);
    };

    // 立即返回 session_id，前端通过 GET 订阅进度
    nlohmann::json resp;
    resp["session_id"] = session->_sessionId;
    resp["status"] = "started";
    res.set_content(resp.dump(), "application/json");

    // 后台训练线程：执行所有训练阶段，通过 sendSSE 推送进度
    std::thread trainThread([state, session, sendSSE, params, this,
        script, labelCfg, xgbParams, dateRangeCfg, testRatio, valRatio,
        labelSource, labelPeriod, labelType, labelShape, volK, objective, numClass,
        startDate, endDate, frequency, strategyName, modelType]() mutable {

        auto cleanupGraph = [&]() { for (auto n : state->_fullGraph) delete n; };
        state->_tmpStrategyName = strategyName + "_train";

        try {

        // 检查是否提供了预收集的 CSV（跳过步骤 2~5）
        String csvPath = params.value("csv_path", String());
        if (!csvPath.empty()) {
            state->_csvPath = csvPath;
            INFO("[MLTrain] 使用预收集 CSV: {}", csvPath);
            sendSSE("step", {{"step","parse_script"},{"status","done"},{"msg","跳过（使用预收集数据）"}});
            sendSSE("step", {{"step","init_nodes"},{"status","done"},{"msg","跳过"}});
            sendSSE("step", {{"step","start_exchange"},{"status","done"},{"msg","跳过"}});
            sendSSE("step", {{"step","collect_data"},{"status","done"},{"msg","跳过（使用预收集数据）"}});
        } else {

        // ============== 2. 解析策略图 ==============
        sendSSE("step", {{"step","parse_script"},{"status","start"},{"msg","解析策略图..."}});
        try {
            state->_fullGraph = parse_strategy_script_v2(script, _server);
            state->_fullGraph = topo_sort(state->_fullGraph);
        } catch (const std::exception& e) {
            cleanupGraph();
            sendSSE("error", {{"step","parse_script"},{"msg", String("strategy parse failed: ") + e.what()}});
            session->finish({{"error", String("strategy parse failed: ") + e.what()}}, true);
            return;
        }
        sendSSE("step", {{"step","parse_script"},{"status","done"}});

        state->_upstreamSet = collectUpstreamNodes(state->_fullGraph);
        if (state->_upstreamSet.empty()) {
            cleanupGraph();
            sendSSE("error", {{"step","parse_script"},{"msg","未找到 XGBoost 节点或上游子图为空"}});
            session->finish({{"error","未找到 XGBoost 节点或上游子图为空"}}, true); return;
        }
        for (auto n : state->_fullGraph) {
            if (state->_upstreamSet.count(n)) state->_upstreamSubgraph.push_back(n);
        }

        // ============== 3. Init 上游节点 ==============
        sendSSE("step", {{"step","init_nodes"},{"status","start"},{"msg","初始化上游节点..."}});
        try {
            std::map<uint32_t, nlohmann::json> nodeConfigMap;
            for (auto& node : script["nodes"]) {
                uint32_t id = atoi(node["id"].get<std::string>().c_str());
                nodeConfigMap[id] = node["data"];
            }
            for (auto n : state->_upstreamSubgraph) {
                auto cfgItr = nodeConfigMap.find(n->id());
                if (cfgItr != nodeConfigMap.end()) {
                    try {
                        n->Init(cfgItr->second);
                    } catch (const std::exception& initEx) {
                        sendSSE("warning", {{"step","init_nodes"},{"msg", String(initEx.what())}});
                        throw;
                    }
                }
            }
            collectXGBoostFeatures(state->_upstreamSubgraph, state->_featureNames);
        } catch (const std::exception& e) {
            cleanupGraph();
            sendSSE("error", {{"step","init_nodes"},{"msg", String("node init failed: ") + e.what()}});
            session->finish({{"error","未找到 XGBoost 节点或上游子图为空"}}, true); return;
        }
        sendSSE("step", {{"step","init_nodes"},{"status","done"},{"features",(int)state->_featureNames.size()}});

        // ============== 4. 启动 Exchange ==============
        sendSSE("step", {{"step","start_exchange"},{"status","start"},{"msg","启动数据源..."}});
        auto* exchangeMgr = _server->GetExchangeManager();
        if (!exchangeMgr) {
            cleanupGraph();
            sendSSE("error", {{"step","start_exchange"},{"msg","ExchangeManager unavailable"}});
            session->finish({{"error","未找到 XGBoost 节点或上游子图为空"}}, true); return;
        }
        Set<String> requiredSources = sourcesFromNodes(state->_upstreamSubgraph);
        exchangeMgr->StartRequiredExchanges(requiredSources);
        Set<symbol_t> symbols;
        for (auto n : state->_upstreamSubgraph) {
            if (auto* qn = dynamic_cast<QuoteInputNode*>(n)) {
                for (auto s : qn->GetSymbols()) symbols.insert(s);
            }
        }
        if (symbols.empty()) {
            cleanupGraph();
            sendSSE("error", {{"step","start_exchange"},{"msg","未找到可用的 symbols"}});
            session->finish({{"error","未找到 XGBoost 节点或上游子图为空"}}, true); return;
        }
        sendSSE("step", {{"step","start_exchange"},{"status","done"},{"symbols",(int)symbols.size()}});

        // ============== 5. 数据收集 ==============
        sendSSE("step", {{"step","collect_data"},{"status","start"},{"msg","收集特征数据..."}});
        INFO("[MLTrain] === Collect start: strategy='{}', symbols={}, nodes={}, sources={}",
             state->_tmpStrategyName, symbols.size(), state->_upstreamSubgraph.size(), requiredSources.size());
        for (auto s : symbols) {
            INFO("[MLTrain]   symbol: {}", s);
        }
        for (auto& src : requiredSources) {
            INFO("[MLTrain]   source: {}", src);
        }
        auto* flowSubsystem = _server->GetStrategySystem()->GetFlowSubsystem();
        Map<String, Vector<double>> collected;
        Vector<String> collectedDates;
        bool collectOk = flowSubsystem->RunTrainingCollect(
            state->_tmpStrategyName, state->_upstreamSubgraph, requiredSources,
            symbols, 100000.0, collected, collectedDates,
            [sendSSE](uint64_t epoch, uint64_t totalBars) {
                sendSSE("progress", {
                    {"step","collect_data"},
                    {"current",(int)epoch},
                    {"total",(int)totalBars}
                });
            },
            startDate, endDate
        );

        INFO("[MLTrain] === Collect done: ok={}, collected_keys={}, dates={}",
             collectOk, collected.size(), collectedDates.size());
        if (!collected.empty()) {
            for (auto& [k, v] : collected) {
                INFO("[MLTrain]   key='{}' points={}", k, v.size());
            }
            if (!collectedDates.empty()) {
                INFO("[MLTrain]   date range: {} -> {}", collectedDates.front(), collectedDates.back());
            }
        }
        if (!collectOk || collected.empty()) {
            cleanupGraph();
            sendSSE("error", {{"step","collect_data"},{"msg","数据收集失败，请确认 Quote 节点已配置标的数据"}});
            session->finish({{"error","未找到 XGBoost 节点或上游子图为空"}}, true); return;
        }
        if (!labelSource.empty() && collected.find(labelSource) == collected.end()) {
            String avail; int cnt = 0;
            for (auto& [k, _] : collected) { if (cnt++ > 0) avail += ", "; avail += k; }
            cleanupGraph();
            sendSSE("error", {{"step","collect_data"},{"msg", String("label.source '") + labelSource + "' not found. Available: " + avail}});
            session->finish({{"error","未找到 XGBoost 节点或上游子图为空"}}, true); return;
        }
        // 过滤 collected：只保留 XGBoostNode 直接上游的特征列（与 _featureNames 一致）
        if (!state->_featureNames.empty()) {
            auto [filtered, droppedKeys] = filterCollectedData(collected, state->_featureNames, labelSource, labelShape);
            INFO("[MLTrain] Filter: collected {} columns, kept {}, dropped {}: [{}]",
                 collected.size(), filtered.size(), droppedKeys.size(),
                 fmt::join(droppedKeys, ", "));
            {
                Vector<String> ks;
                ks.reserve(filtered.size());
                for (auto& [k, _] : filtered) ks.push_back(k);
                INFO("[MLTrain] Filter: filtered columns (will be in CSV): [{}]", fmt::join(ks, ", "));
            }
            collected = std::move(filtered);
        }
        sendSSE("step", {{"step","collect_data"},{"status","done"},{"bars",(int)collectedDates.size()},{"features",(int)collected.size()}});

        // 写 CSV（仅在新收集数据时）
        // 列顺序按 XGBoostNode 推理侧特征顺序（_featureNames，节点 id 排序），保证训练与推理特征顺序一致
        state->_csvPath = makeTempPath("xgb_data", "csv");
        writeCsv(state->_csvPath, collected, collectedDates, &state->_featureNames);
        INFO("[MLTrain] 训练数据 CSV: {}", state->_csvPath);

        } // end else (no csv_path)

        // ============== 6. Python 训练 ==============
        sendSSE("step", {{"step","train_model"},{"status","start"},{"msg","Python 训练..."}});
        state->_modelPath = makeTempPath("xgb_model", "json");

        auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
        auto interpreter = pyEnv.resolve(params.value("py_env", ""));
        std::vector<std::string> args = {
            "--data", state->_csvPath, "--label-source", labelSource,
            "--label-period", std::to_string(labelPeriod), "--label-type", labelType,
            "--label-shape", labelShape,
            "--vol-k", std::to_string(volK), "--objective", objective,
            "--num-class", std::to_string(numClass), "--model-output", state->_modelPath,
            "--params", xgbParams.dump(), "--test-ratio", std::to_string(testRatio),
            "--val-ratio", std::to_string(valRatio),
            "--frequency", frequency,
        };
        if (!startDate.empty()) args.insert(args.end(), {"--start-date", startDate});
        if (!endDate.empty()) args.insert(args.end(), {"--end-date", endDate});
        PythonRunner runner;
        INFO("[MLTrain] Starting Python training: interpreter='{}', script='{}', data='{}'", interpreter, "tools/xgboost_train.py", state->_csvPath);
        if (!runner.start("tools/xgboost_train.py", args, interpreter)) {
            WARN("[MLTrain] PythonRunner::start() failed");
            cleanupGraph();
            sendSSE("error", {{"step","train_model"},{"msg","failed to start training script"}});
            session->finish({{"error","未找到 XGBoost 节点或上游子图为空"}}, true); return;
        }
        PythonOutput out;
        String resultLine, stderrLines;
        while (runner.readLine(out, 60000)) {
            if (out.type == PythonOutput::DONE) break;
            if (out.type == PythonOutput::STDOUT) {
                if (out.line.find("\"type\":\"result\"") != std::string::npos) resultLine = out.line;
                else if (out.line.find("\"type\":\"error\"") != std::string::npos) {
                    WARN("[MLTrain error] {}", to_utf8(out.line));
                    sendSSE("warning", {{"step","train_model"},{"line",out.line}});
                    if (stderrLines.size() < 1000) stderrLines += out.line + "\n";
                }
                else if (out.line.find("\"type\":\"progress\"") != std::string::npos
                      || out.line.find("\"type\":\"info\"") != std::string::npos)
                    sendSSE("log", {{"step","train_model"},{"line",out.line}});
            } else if (out.type == PythonOutput::STDERR) {
                WARN("[MLTrain stderr] {}", to_utf8(out.line));
                sendSSE("warning", {{"step","train_model"},{"line",out.line}});
                if (stderrLines.size() < 1000) stderrLines += out.line + "\n";
            }
        }
        cleanupGraph();

        if (resultLine.empty()) {
            String msg = stderrLines.empty() ? "训练脚本未输出 result" : stderrLines.substr(0, 500);
            while (!msg.empty() && msg.back() == '\n') msg.pop_back();
            sendSSE("error", {{"step","train_model"},{"msg", String("训练失败: ") + msg}});
            // 保留 CSV 供调试分析
            session->finish({{"error","未找到 XGBoost 节点或上游子图为空"}}, true); return;
        }

        nlohmann::json trainResult;
        try { trainResult = nlohmann::json::parse(resultLine); }
        catch (...) {
            sendSSE("error", {{"step","train_model"},{"msg", String("训练结果解析失败: ") + resultLine.substr(0, 200)}});
            // 保留 CSV 供调试分析
            std::remove(state->_modelPath.c_str());
            session->finish({{"error","未找到 XGBoost 节点或上游子图为空"}}, true); return;
        }
        sendSSE("step", {{"step","train_model"},{"status","done"}});

        // ============== 7. 持久化模型 + 8. 加载模型 ==============
        try {
            String dbPath = _server->GetConfig().GetDatabasePath();
            String expDir = dbPath + "/models/experiments";
            std::filesystem::create_directories(expDir);
            time_t now = Now();
            String ts = ToString(now, "%Y%m%d_%H%M%S");
            String persistName = strategyName + "_" + ts;
            String persistPath = expDir + "/" + persistName + ".json";
            std::filesystem::copy(state->_modelPath, persistPath, std::filesystem::copy_options::overwrite_existing);
            {
                nlohmann::json meta;
                meta["strategy_id"] = strategyName;
                meta["created_at"] = ToString(now, "%Y-%m-%dT%H:%M:%S");
                meta["source"] = "experiment";
                meta["model_type"] = modelTypeToString(modelType);
                meta["label"] = labelCfg; meta["objective"] = objective;
                meta["num_class"] = numClass; meta["test_ratio"] = testRatio; meta["val_ratio"] = valRatio;
                meta["params"] = xgbParams; meta["date_range"] = dateRangeCfg;
                meta["features"] = state->_featureNames;
                if (trainResult.contains("eval_metrics")) meta["eval_metrics"] = trainResult["eval_metrics"];
                if (trainResult.contains("n_train")) meta["n_train"] = trainResult["n_train"];
                if (trainResult.contains("n_val")) meta["n_val"] = trainResult["n_val"];
                if (trainResult.contains("n_test")) meta["n_test"] = trainResult["n_test"];
                meta["n_features"] = state->_featureNames.size();
                std::ofstream ofs(expDir + "/" + persistName + ".meta.json");
                if (ofs.is_open()) ofs << meta.dump(2);
            }

            BoosterHandle booster = nullptr;
            if (XGBoosterCreate(nullptr, 0, &booster) == 0 && booster) {
                if (XGBoosterLoadModel(booster, state->_modelPath.c_str()) != 0) {
                    XGBoosterFree(booster); booster = nullptr;
                }
            }
            // X_test: 行=样本, 列=特征；先确定行/列数，再 resize 后逐元素填充
            Eigen::MatrixXd Xtest;
            if (trainResult.contains("X_test") && trainResult["X_test"].is_array()) {
                size_t nRows = trainResult["X_test"].size();
                size_t nCols = 0;
                if (nRows > 0 && trainResult["X_test"][0].is_array())
                    nCols = trainResult["X_test"][0].size();
                Xtest.resize(nRows, nCols);
                for (size_t i = 0; i < nRows; ++i) {
                    const auto& row = trainResult["X_test"][i];
                    for (size_t j = 0; j < nCols && j < row.size(); ++j) {
                        Xtest(i, j) = row[j].get<double>();
                    }
                }
            }
            // X_test_dates: 与 X_test 行对齐的日期字符串
            Vector<String> xTestDates;
            if (trainResult.contains("X_test_dates") && trainResult["X_test_dates"].is_array()) {
                for (const auto& d : trainResult["X_test_dates"]) {
                    xTestDates.push_back(d.get<String>());
                }
            }
            uint64_t modelId = 0;
            if (booster) {
                Vector<String> actualFeatures;
                if (trainResult.contains("features") && trainResult["features"].is_array())
                    for (auto& f : trainResult["features"]) actualFeatures.push_back(f.get<String>());
                else actualFeatures = state->_featureNames;
                modelId = registerModel(modelType, booster, actualFeatures, Xtest);
                trainResult["model_id"] = modelId;
                trainResult["model_type"] = modelTypeToString(modelType);
                if (auto* m = getModel(modelId)) {
                    m->_modelPath = persistPath;
                    m->_x_test_dates = std::move(xTestDates);
                }
                // 回写 model_id 到 meta.json（注册前写入的 meta 没有 model_id）
                {
                    String metaPath = expDir + "/" + persistName + ".meta.json";
                    if (std::filesystem::exists(metaPath)) {
                        try {
                            auto m = nlohmann::json::parse(std::ifstream(metaPath));
                            m["model_id"] = modelId;
                            std::ofstream ofs(metaPath);
                            if (ofs.is_open()) ofs << m.dump(2);
                        } catch (...) {}
                    }
                }
            }
            trainResult.erase("X_test");
            trainResult.erase("X_test_dates");
            trainResult["model_path"] = persistPath;
            // 保留 CSV 供调试分析（临时目录由系统定期清理）
            std::remove(state->_modelPath.c_str());

            sendSSE("result", trainResult);
            session->finish(trainResult);

        } catch (const std::exception& e) {
            WARN("[MLTrain] 模型持久化/加载异常: {}", e.what());
            sendSSE("error", {{"step","train_model"},{"msg", String("模型持久化失败: ") + e.what()}});
            trainResult.erase("X_test");
            trainResult.erase("X_test_dates");
            sendSSE("result", trainResult);
            session->finish(trainResult);
        }

        } catch (const std::exception& e) {
            cleanupGraph();
            FATAL("[MLTrain] uncaught exception: {}", e.what());
            sendSSE("error", {{"step","train"},{"msg", String("internal error: ") + e.what()}});
            session->finish({{"error", String("internal error: ") + e.what()}}, true);
        } catch (...) {
            cleanupGraph();
            FATAL("[MLTrain] unknown uncaught exception");
            sendSSE("error", {{"step","train"},{"msg","unknown internal error"}});
            session->finish({{"error","unknown internal error"}}, true);
        }
    });
    trainThread.detach();
}

// ============== 特征统计辅助函数 ==============
static nlohmann::json computeFeatureStats(const Map<String, Vector<double>>& collected,
                                           const Vector<String>& dates) {
    nlohmann::json stats;
    stats["total_rows"] = dates.size();
    stats["n_features"] = collected.size();
    if (!dates.empty()) {
        stats["date_start"] = dates.front();
        stats["date_end"] = dates.back();
    }

    nlohmann::json features = nlohmann::json::array();
    for (auto& [name, values] : collected) {
        nlohmann::json fs;
        fs["name"] = name;

        size_t total = values.size();
        size_t nanCount = 0;
        double sum = 0, sumSq = 0;
        double mn = std::numeric_limits<double>::max();
        double mx = std::numeric_limits<double>::lowest();
        Vector<double> validVals;
        validVals.reserve(total);

        for (size_t i = 0; i < total; ++i) {
            double v = values[i];
            if (v != v || std::isinf(v)) {  // NaN or Inf
                ++nanCount;
                continue;
            }
            validVals.push_back(v);
            sum += v;
            sumSq += v * v;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }

        fs["valid"] = validVals.size();
        fs["nan_count"] = nanCount;
        fs["nan_pct"] = total > 0 ? (double)nanCount / total * 100.0 : 0.0;

        if (validVals.empty()) {
            fs["min"] = nullptr; fs["max"] = nullptr;
            fs["mean"] = nullptr; fs["std"] = nullptr; fs["median"] = nullptr;
        } else {
            double mean = sum / validVals.size();
            double variance = sumSq / validVals.size() - mean * mean;
            double stdDev = variance > 0 ? std::sqrt(variance) : 0.0;
            std::sort(validVals.begin(), validVals.end());
            double median = validVals.size() % 2 == 0
                ? (validVals[validVals.size()/2 - 1] + validVals[validVals.size()/2]) / 2.0
                : validVals[validVals.size()/2];

            fs["min"] = mn;
            fs["max"] = mx;
            fs["mean"] = mean;
            fs["std"] = stdDev;
            fs["median"] = median;
        }
        features.push_back(fs);
    }
    stats["features"] = features;

    // 输出原始时序（用于前端绘制折线图和异常检测）
    nlohmann::json series;
    series["dates"] = dates;
    nlohmann::json data = nlohmann::json::object();
    for (auto& [name, values] : collected) {
        nlohmann::json arr = nlohmann::json::array();
        arr.get_ref<nlohmann::json::array_t&>().reserve(values.size());
        for (double v : values) {
            if (std::isnan(v) || std::isinf(v)) {
                arr.push_back(nullptr);  // NaN/Inf → JSON null
            } else {
                arr.push_back(v);
            }
        }
        data[name] = std::move(arr);
    }
    series["data"] = std::move(data);
    stats["series"] = std::move(series);

    return stats;
}

void MLHandler::handleCollect(const nlohmann::json& params, httplib::Response& res) {
    // 先解析 script（在发送任何响应之前，以便失败时能返回 400）
    String strScript = params.value("script", "");
    nlohmann::json script;
    try {
        script = nlohmann::json::parse(strScript);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"invalid 'script' JSON"})", "application/json");
        return;
    }

    // 幂等检查
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        if (s_activeSession && !s_activeSession->_done) {
            nlohmann::json resp;
            resp["session_id"] = s_activeSession->_sessionId;
            resp["status"] = "running";
            res.set_content(resp.dump(), "application/json");
            return;
        }
    }

    auto session = std::make_shared<TrainSession>();
    session->_sessionId = fmt::format("xgb_{}", std::chrono::steady_clock::now().time_since_epoch().count());
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        s_activeSession = session;
    }

    nlohmann::json resp;
    resp["session_id"] = session->_sessionId;
    resp["status"] = "started";
    res.set_content(resp.dump(), "application/json");

    // 提取其余参数
    auto labelCfg = params.value("label", nlohmann::json::object());
    auto dateRangeCfg = params.value("date_range", nlohmann::json::object());
    String labelSource = labelCfg.value("source", "");
    String labelShape = labelCfg.value("shape", "matrix");
    String startDate = dateRangeCfg.value("start", "");
    String endDate = dateRangeCfg.value("end", "");
    String frequency = dateRangeCfg.value("frequency", "1d");
    String strategyName = params.value("strategy_name", String("default"));

    std::thread collectThread([session, params, script, labelCfg, dateRangeCfg,
        labelSource, labelShape, startDate, endDate, frequency, strategyName, this]() mutable {

        auto state = std::make_shared<TrainState>();
        auto sendSSE = [session](const String& type, const nlohmann::json& data) {
            session->pushEvent(type, data);
        };
        auto cleanupGraph = [&]() { for (auto n : state->_fullGraph) delete n; };
        state->_tmpStrategyName = strategyName + "_collect";

        try {

        // ============== 1. 解析策略图 ==============
        sendSSE("step", {{"step","parse_script"},{"status","start"},{"msg","解析策略图..."}});
        try {
            state->_fullGraph = parse_strategy_script_v2(script, _server);
            state->_fullGraph = topo_sort(state->_fullGraph);
        } catch (const std::exception& e) {
            cleanupGraph();
            sendSSE("error", {{"step","parse_script"},{"msg", String("strategy parse failed: ") + e.what()}});
            session->finish({{"error", String("strategy parse failed: ") + e.what()}}, true);
            return;
        }
        sendSSE("step", {{"step","parse_script"},{"status","done"}});

        state->_upstreamSet = collectUpstreamNodes(state->_fullGraph);
        if (state->_upstreamSet.empty()) {
            cleanupGraph();
            sendSSE("error", {{"step","parse_script"},{"msg","未找到 XGBoost 节点或上游子图为空"}});
            session->finish({{"error","上游子图为空"}}, true); return;
        }
        for (auto n : state->_fullGraph) {
            if (state->_upstreamSet.count(n)) state->_upstreamSubgraph.push_back(n);
        }

        // ============== 2. Init 上游节点 ==============
        sendSSE("step", {{"step","init_nodes"},{"status","start"},{"msg","初始化上游节点..."}});
        try {
            std::map<uint32_t, nlohmann::json> nodeConfigMap;
            for (auto& node : script["nodes"]) {
                uint32_t id = atoi(node["id"].get<std::string>().c_str());
                nodeConfigMap[id] = node["data"];
            }
            for (auto n : state->_upstreamSubgraph) {
                auto cfgItr = nodeConfigMap.find(n->id());
                if (cfgItr != nodeConfigMap.end()) {
                    try { n->Init(cfgItr->second); }
                    catch (const std::exception& initEx) {
                        sendSSE("warning", {{"step","init_nodes"},{"msg", String(initEx.what())}});
                        throw;
                    }
                }
            }
            collectXGBoostFeatures(state->_upstreamSubgraph, state->_featureNames);
        } catch (const std::exception& e) {
            cleanupGraph();
            sendSSE("error", {{"step","init_nodes"},{"msg", String("node init failed: ") + e.what()}});
            session->finish({{"error", String("node init failed: ") + e.what()}}, true); return;
        }
        sendSSE("step", {{"step","init_nodes"},{"status","done"},{"features",(int)state->_featureNames.size()}});

        // ============== 3. 启动 Exchange ==============
        sendSSE("step", {{"step","start_exchange"},{"status","start"},{"msg","启动数据源..."}});
        auto* exchangeMgr = _server->GetExchangeManager();
        if (!exchangeMgr) {
            cleanupGraph();
            sendSSE("error", {{"step","start_exchange"},{"msg","ExchangeManager unavailable"}});
            session->finish({{"error","ExchangeManager unavailable"}}, true); return;
        }
        Set<String> requiredSources = sourcesFromNodes(state->_upstreamSubgraph);
        exchangeMgr->StartRequiredExchanges(requiredSources);
        Set<symbol_t> symbols;
        for (auto n : state->_upstreamSubgraph) {
            if (auto* qn = dynamic_cast<QuoteInputNode*>(n)) {
                for (auto s : qn->GetSymbols()) symbols.insert(s);
            }
        }
        if (symbols.empty()) {
            cleanupGraph();
            sendSSE("error", {{"step","start_exchange"},{"msg","未找到可用的 symbols"}});
            session->finish({{"error","未找到可用的 symbols"}}, true); return;
        }
        sendSSE("step", {{"step","start_exchange"},{"status","done"},{"symbols",(int)symbols.size()}});

        // ============== 4. 数据收集 ==============
        sendSSE("step", {{"step","collect_data"},{"status","start"},{"msg","收集特征数据..."}});
        auto* flowSubsystem = _server->GetStrategySystem()->GetFlowSubsystem();
        Map<String, Vector<double>> collected;
        Vector<String> collectedDates;
        // 特征收集只需跑 XGBoostNode 的上游节点，不跑推理
        List<QNode*> collectGraph;
        for (auto n : state->_upstreamSubgraph) {
            if (!dynamic_cast<XGBoostNode*>(n)) {
                collectGraph.push_back(n);
            }
        }
        bool collectOk = flowSubsystem->RunTrainingCollect(
            state->_tmpStrategyName, collectGraph, requiredSources,
            symbols, 100000.0, collected, collectedDates,
            [sendSSE](uint64_t epoch, uint64_t totalBars) {
                sendSSE("progress", {
                    {"step","collect_data"},
                    {"current",(int)epoch},
                    {"total",(int)totalBars}
                });
            },
            startDate, endDate
        );
        if (!collectOk || collected.empty()) {
            cleanupGraph();
            sendSSE("error", {{"step","collect_data"},{"msg","数据收集失败"}});
            session->finish({{"error","数据收集失败"}}, true); return;
        }
        // 过滤 collected：只保留 XGBoostNode 直接上游的特征列
        if (!state->_featureNames.empty()) {
            auto [filtered, droppedKeys] = filterCollectedData(collected, state->_featureNames, "", labelShape);
            INFO("[MLCollect] Filter: collected {} columns, kept {}, dropped {}: [{}]",
                 collected.size(), filtered.size(), droppedKeys.size(),
                 fmt::join(droppedKeys, ", "));
            // {
            //     Vector<String> ks;
            //     ks.reserve(filtered.size());
            //     for (auto& [k, _] : filtered) ks.push_back(k);
            //     INFO("[MLCollect] Filter: filtered columns (will be in CSV): [{}]", fmt::join(ks, ", "));
            // }
            collected = std::move(filtered);
        }
        sendSSE("step", {{"step","collect_data"},{"status","done"},{"bars",(int)collectedDates.size()},{"features",(int)collected.size()}});

        // ============== 5. 计算特征统计 + 写 CSV ==============
        sendSSE("step", {{"step","analyze"},{"status","start"},{"msg","分析特征数据..."}});
        auto featureStats = computeFeatureStats(collected, collectedDates);

        state->_csvPath = makeTempPath("xgb_data", "csv");
        writeCsv(state->_csvPath, collected, collectedDates, &state->_featureNames);
        INFO("[MLCollect] CSV: {}", state->_csvPath);

        featureStats["csv_path"] = state->_csvPath;
        sendSSE("step", {{"step","analyze"},{"status","done"}});
        sendSSE("feature_stats", featureStats);

        cleanupGraph();
        session->finish(featureStats);

        } catch (const std::exception& e) {
            cleanupGraph();
            FATAL("[MLCollect] uncaught exception: {}", e.what());
            sendSSE("error", {{"step","collect"},{"msg", String("internal error: ") + e.what()}});
            session->finish({{"error", String("internal error: ") + e.what()}}, true);
        } catch (...) {
            cleanupGraph();
            FATAL("[MLCollect] unknown uncaught exception");
            sendSSE("error", {{"step","collect"},{"msg","unknown internal error"}});
            session->finish({{"error","unknown internal error"}}, true);
        }
    });
    collectThread.detach();
}

void MLHandler::handleTrainProgress(const httplib::Request& req, httplib::Response& res) {
    String sessionId = req.get_param_value("session_id");
    if (sessionId.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing session_id"})", "application/json");
        return;
    }

    // 查找会话
    std::shared_ptr<TrainSession> session;
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        if (s_activeSession && s_activeSession->_sessionId == sessionId) {
            session = s_activeSession;
        }
    }
    if (!session) {
        res.status = 404;
        res.set_content(R"({"message":"session not found"})", "application/json");
        return;
    }

    // SSE 流：先回放历史事件，再实时推送新事件
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*");

    size_t replayIdx = 0;
    res.set_chunked_content_provider("text/event-stream",
        [session, replayIdx](size_t /*offset*/, httplib::DataSink& sink) mutable -> bool {
            if (!sink.is_writable()) return false;

            while (true) {
                std::unique_lock<std::mutex> lk(session->_mtx);

                // 回放历史事件
                if (replayIdx < session->_eventLog.size()) {
                    auto& ev = session->_eventLog[replayIdx++];
                    String msg = "event:" + ev._type + "\ndata:" + ev._dataStr + "\n\n";
                    lk.unlock();
                    sink.write(msg.c_str(), msg.size());
                    return true;
                }

                // 训练已完成，发送最终结果后关闭
                if (session->_done) {
                    lk.unlock();
                    sink.done();
                    return false;
                }

                // 等待新事件
                session->_cv.wait_for(lk, std::chrono::milliseconds(500));
            }
        });
}

void MLHandler::handleTrainStatus(const httplib::Request& req, httplib::Response& res) {
    String sessionId = req.get_param_value("session_id");
    if (sessionId.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing session_id"})", "application/json");
        return;
    }

    std::shared_ptr<TrainSession> session;
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        if (s_activeSession && s_activeSession->_sessionId == sessionId) {
            session = s_activeSession;
        }
    }
    if (!session) {
        res.status = 404;
        res.set_content(R"({"message":"session not found"})", "application/json");
        return;
    }

    nlohmann::json resp;
    std::lock_guard<std::mutex> lk(session->_mtx);
    if (!session->_done) {
        resp["status"] = "running";
    } else if (session->_hasError) {
        resp = session->_result;
        resp["status"] = "error";
    } else {
        resp = session->_result;
        resp["status"] = "done";
    }
    res.set_content(resp.dump(), "application/json");
}

void MLHandler::handleShap(const nlohmann::json& params, httplib::Response& res) {
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
    if (!model || !model->_booster) {
        res.status = 404;
        res.set_content(R"({"message":"model not found"})", "application/json");
        return;
    }

    if (model->_x_test.size() == 0) {
        res.status = 400;
        res.set_content(R"({"message":"model has no test data"})", "application/json");
        return;
    }

    size_t n_features = model->_features.size();
    size_t n_total = static_cast<size_t>(model->_x_test.rows());

    // 解析可选的日期过滤参数
    String startDate, endDate;
    if (params.contains("start_date")) startDate = params["start_date"].get<String>();
    if (params.contains("end_date")) endDate = params["end_date"].get<String>();

    // 构建行索引过滤（日期范围）
    std::vector<size_t> rowIndices;
    const auto& dates = model->_x_test_dates;
    bool hasDateFilter = !startDate.empty() || !endDate.empty();
    bool hasDates = dates.size() == n_total;

    if (hasDateFilter && hasDates) {
        for (size_t i = 0; i < n_total; ++i) {
            const auto& d = dates[i];
            if (!startDate.empty() && d < startDate) continue;
            if (!endDate.empty() && d > endDate) continue;
            rowIndices.push_back(i);
        }
    } else {
        rowIndices.resize(n_total);
        std::iota(rowIndices.begin(), rowIndices.end(), 0);
    }

    size_t n_samples = rowIndices.size();
    if (n_samples == 0) {
        res.status = 400;
        res.set_content(R"({"message":"no samples in date range"})", "application/json");
        return;
    }

    std::vector<float> flat(n_samples * n_features);
    for (size_t k = 0; k < n_samples; ++k) {
        size_t i = rowIndices[k];
        for (size_t j = 0; j < n_features; ++j) {
            double v = (j < static_cast<size_t>(model->_x_test.cols())) ? model->_x_test(i, j) : 0.0;
            flat[k * n_features + j] = static_cast<float>(v);
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
    INFO("[ML SHAP] Before predict: booster={}, dmat={}, n_samples={}, n_features={}", 
         (void*)model->_booster, (void*)dmat, n_samples, n_features);

#if XGBOOST_VER_MAJOR >= 2
    // XGBoost >= 2.x: 使用 config JSON 字符串
    // PredictionType: kValue=0, kMargin=1, kContribution=2 (SHAP), kApproxContribution=3, kInteraction=4
    const char* predict_config = R"({"type": 2, "training": false, "iteration_begin": 0, "iteration_end": 0, "strict_shape": true})";
    bst_ulong const* out_shape = nullptr;
    
    ret = XGBoosterPredictFromDMatrix(model->_booster, dmat,
                                       predict_config,
                                       &out_shape, &out_dim, &out_data);
    
    // 调试日志：调用后
    INFO("[ML SHAP] After predict: ret={}, out_dim={}, out_data={}, out_shape={}",
         ret, out_dim, (void*)out_data, (void*)out_shape);
    if (ret != 0 || !out_data) {
        FATAL("[ML SHAP] XGBoost error: {}", XGBGetLastError());
    }
    if (out_shape && out_dim > 0) {
        String shapeStr = "out_shape=[";
        for (bst_ulong d = 0; d < out_dim; ++d) {
            if (d > 0) shapeStr += ", ";
            shapeStr += std::to_string(out_shape[d]);
        }
        shapeStr += "]";
        INFO("[ML SHAP] {}", shapeStr);
    }
    
    // out_shape[0] = 总元素数 = n_samples × out_dim；n_samples 沿用预设值
    n_out = n_samples * out_dim;
#else
    // XGBoost < 2.x: 旧版 (option, ntree_limit, training) 参数
    bst_ulong out_n = 0;
    float* out_data_mut = nullptr;
    ret = XGBoosterPredictFromDMatrix(model->_booster, dmat,
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
    for (auto& f : model->_features) featuresArr.push_back(f);

    // 返回过滤后的日期数组
    nlohmann::json datesArr = nlohmann::json::array();
    if (hasDates) {
        for (size_t k = 0; k < n_samples; ++k) {
            datesArr.push_back(model->_x_test_dates[rowIndices[k]]);
        }
    }

    nlohmann::json resp = {
        {"model_id", modelId},
        {"features", featuresArr},
        {"shap", shapList},
        {"base_value", baseList},
        {"n_samples", n_samples},
        {"dates", datesArr},
    };
    res.set_content(resp.dump(), "application/json");
}

void MLHandler::handleList(const httplib::Request& req, httplib::Response& res) {
    String dbPath = _server->GetConfig().GetDatabasePath();
    String expDir = dbPath + "/models/experiments";
    String prodDir = dbPath + "/models/production";
    String filterStrategy = req.get_param_value("strategy_id");

    nlohmann::json experiments = nlohmann::json::array();

    if (std::filesystem::exists(expDir) && std::filesystem::is_directory(expDir)) {
        Vector<std::filesystem::path> modelFiles;
        for (auto& entry : std::filesystem::directory_iterator(expDir)) {
            String fname = entry.path().filename().string();
            if (fname.size() > 5 && fname.substr(fname.size() - 5) == ".json"
                && fname.find(".meta.json") == String::npos) {
                modelFiles.push_back(entry.path());
            }
        }
        std::sort(modelFiles.begin(), modelFiles.end(), std::greater<>());

        for (auto& fp : modelFiles) {
            nlohmann::json item;
            item["path"] = fp.string();
            item["filename"] = fp.filename().string();

            nlohmann::json meta;
            String metaPath = fp.string();
            auto dotPos = metaPath.rfind('.');
            if (dotPos != String::npos) {
                metaPath = metaPath.substr(0, dotPos) + ".meta.json";
            }
            if (std::filesystem::exists(metaPath)) {
                std::ifstream ifs(metaPath);
                if (ifs.is_open()) {
                    try {
                        meta = nlohmann::json::parse(ifs);
                        item["meta"] = meta;
                    } catch (...) {
                        item["meta"] = nullptr;
                    }
                }
            }

            if (!filterStrategy.empty() && meta.value("strategy_id", "") != filterStrategy)
                continue;
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

                nlohmann::json meta;
                String metaPath = entry.path().string();
                auto dotPos = metaPath.rfind('.');
                if (dotPos != String::npos) {
                    metaPath = metaPath.substr(0, dotPos) + ".meta.json";
                }
                if (std::filesystem::exists(metaPath)) {
                    std::ifstream ifs(metaPath);
                    if (ifs.is_open()) {
                        try {
                            meta = nlohmann::json::parse(ifs);
                            item["meta"] = meta;
                        } catch (...) {
                            item["meta"] = nullptr;
                        }
                    }
                }

                if (!filterStrategy.empty() && meta.value("strategy_id", "") != filterStrategy)
                    continue;
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

void MLHandler::handleDelete(uint64_t modelId, httplib::Response& res) {
    String modelPath;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(_mtx);
        auto itr = _cache.find(modelId);
        if (itr != _cache.end()) {
            found = true;
            modelPath = itr->second._modelPath;
            itr->second.clear();
            _cache.erase(itr);
        }
    }
    if (!found) {
        res.status = 404;
        res.set_content(R"({"message":"model not found"})", "application/json");
        return;
    }
    // 同时删除磁盘文件（模型 + meta）
    if (!modelPath.empty() && std::filesystem::exists(modelPath)) {
        std::remove(modelPath.c_str());
        String metaPath = modelPath;
        auto dotPos = metaPath.rfind('.');
        if (dotPos != String::npos) {
            metaPath = metaPath.substr(0, dotPos) + ".meta.json";
        }
        if (std::filesystem::exists(metaPath)) {
            std::remove(metaPath.c_str());
        }
    }
    res.set_content(R"({"message":"deleted"})", "application/json");
}

void MLHandler::post(const httplib::Request& req, httplib::Response& res) {
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
    } else if (action == "optimize") {
        handleOptimize(params, res);
    } else if (action == "collect") {
        handleCollect(params, res);
    } else if (action == "shap") {
        handleShap(params, res);
    } else if (action == "delete") {
        uint64_t modelId = 0;
        try { modelId = params.at("model_id").get<uint64_t>(); }
        catch (...) {
            res.status = 400;
            res.set_content(R"({"message":"invalid model_id"})", "application/json");
            return;
        }
        handleDelete(modelId, res);
    } else if (action == "delete_file") {
        String modelPath = params.value("model_path", "");
        if (modelPath.empty()) {
            res.status = 400;
            res.set_content(R"({"message":"missing model_path"})", "application/json");
            return;
        }
        // 安全检查：路径必须在 experiments 或 production 目录下
        String dbPath = _server->GetConfig().GetDatabasePath();
        String expDir = dbPath + "/models/experiments";
        String prodDir = dbPath + "/models/production";
        if (modelPath.find(expDir) != 0 && modelPath.find(prodDir) != 0) {
            res.status = 403;
            res.set_content(R"({"message":"path not in model directories"})", "application/json");
            return;
        }
        bool deleted = false;
        if (std::filesystem::exists(modelPath)) {
            std::remove(modelPath.c_str());
            deleted = true;
        }
        String metaPath = modelPath;
        auto dotPos = metaPath.rfind('.');
        if (dotPos != String::npos) {
            metaPath = metaPath.substr(0, dotPos) + ".meta.json";
        }
        if (std::filesystem::exists(metaPath)) {
            std::remove(metaPath.c_str());
            deleted = true;
        }
        // 同时清理内存缓存
        if (deleted) {
            std::lock_guard<std::mutex> lock(_mtx);
            for (auto it = _cache.begin(); it != _cache.end(); ) {
                if (it->second._modelPath == modelPath) {
                    it->second.clear();
                    it = _cache.erase(it);
                } else {
                    ++it;
                }
            }
        }
        res.set_content(deleted ? R"({"message":"deleted"})" : R"({"message":"not found"})", "application/json");
    } else {
        res.status = 400;
        res.set_content(
            "{\"message\":\"missing or invalid 'action' (train|optimize|collect|shap|delete)\"}",
            "application/json");
        return;
    }
}

void MLHandler::get(const httplib::Request& req, httplib::Response& res) {
    String action = req.get_param_value("action");
    if (action == "list") {
        handleList(req, res);
    } else if (action == "train") {
        handleTrainProgress(req, res);
    } else if (action == "train_status") {
        handleTrainStatus(req, res);
    } else if (action == "download") {
        // 下载训练产物：model_id → .json + .meta.json，打包成 zip 流式返回
        String modelIdStr = req.get_param_value("model_id");
        if (modelIdStr.empty()) {
            res.status = 400;
            res.set_content(R"({"message":"missing model_id"})", "application/json");
            return;
        }
        uint64_t modelId = 0;
        try { modelId = std::stoull(modelIdStr); }
        catch (...) {
            res.status = 400;
            res.set_content(R"({"message":"invalid model_id"})", "application/json");
            return;
        }
        auto* m = getModel(modelId);
        if (!m || m->_modelPath.empty()) {
            res.status = 404;
            res.set_content(R"({"message":"model not found or path missing"})", "application/json");
            return;
        }
        if (!std::filesystem::exists(m->_modelPath)) {
            res.status = 404;
            res.set_content(R"({"message":"model file not found on disk"})", "application/json");
            return;
        }
        // 流式读取 .json + .meta.json，合并为单一 JSON 响应
        std::ifstream ifs(m->_modelPath, std::ios::binary);
        std::string modelJson((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());
        std::string metaJson;
        String metaPath = m->_modelPath;
        auto dotPos = metaPath.rfind('.');
        if (dotPos != String::npos) {
            metaPath = metaPath.substr(0, dotPos) + ".meta.json";
            if (std::filesystem::exists(metaPath)) {
                std::ifstream mifs(metaPath);
                metaJson = std::string((std::istreambuf_iterator<char>(mifs)),
                                       std::istreambuf_iterator<char>());
            }
        }
        nlohmann::json resp = {
            {"model_id", modelId},
            {"model_path", m->_modelPath},
            {"model_json", modelJson},
            {"meta_json", metaJson},
        };
        res.set_content(resp.dump(), "application/json");
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
            "{\"message\":\"missing or invalid 'action' (list|shap|download)\"}",
            "application/json");
        return;
    }
}

void MLHandler::del(const httplib::Request& req, httplib::Response& res) {
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
