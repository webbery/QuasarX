#include "Handler/HMMHandler.h"
#include "Util/data.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/system.h"
#include "server.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>

namespace fs = std::filesystem;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// DuckDB 返回 "2026-07-03 09:35:00"，统一截成 "2026-07-03" 作为对齐键
String dateKey(const String& dt) {
    return dt.size() > 10 ? dt.substr(0, 10) : dt;
}

/// 文件名片段白名单，防止 strategy_id / model_id 里的 ../ 穿越到模型目录之外
bool isSafeName(const String& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
    }
    return true;
}

String experimentsDir(const String& databasePath) {
    return (fs::path(databasePath) / "hmm_models" / "experiments").string();
}

String productionDir(const String& databasePath) {
    return (fs::path(databasePath) / "hmm_models" / "production").string();
}

void ensureDir(const String& path) {
    fs::create_directories(path);
}

BarFreq parseFreq(const String& freq) {
    if (freq == "1m") return BarFreq::Min1;
    if (freq == "5m") return BarFreq::Min5;
    if (freq == "15m") return BarFreq::Min15;
    if (freq == "30m") return BarFreq::Min30;
    if (freq == "1h") return BarFreq::Hour1;
    return BarFreq::Day;
}

/// 从模型 features 字段还原裸特征名列表
///
/// 模型里存的是 "symbol.feature" 限定名（如 sh.600000.return_1d），因为训练时
/// 需要逐列标识；但 buildObservations 只认裸名，限定名由它自己拼。把限定名直接
/// 传回去会撞上 unknown feature 校验，decode 因此永远无法工作。
/// 取最后一段 '.' 之后的部分，并按首次出现顺序去重。
Vector<String> stripSymbolPrefix(const Vector<String>& qualified) {
    Vector<String> out;
    for (const auto& f : qualified) {
        const auto pos = f.rfind('.');
        const String bare = (pos == String::npos) ? f : f.substr(pos + 1);
        if (std::find(out.begin(), out.end(), bare) == out.end()) out.push_back(bare);
    }
    return out;
}

/// 单个标的的特征序列，全部与 barDates 等长，warmup 期填 kNaN
using FeatureSeries = Map<String, Vector<double>>;

FeatureSeries computeFeatures(const String& feature,
                              const Vector<double>& closes,
                              const Vector<double>& volumes) {
    const size_t n = closes.size();
    FeatureSeries series;
    Vector<double>& out = series[feature];
    out.assign(n, kNaN);

    if (feature == "return_1d") {
        for (size_t i = 1; i < n; ++i) {
            if (closes[i - 1] > 0) out[i] = (closes[i] - closes[i - 1]) / closes[i - 1];
        }
    } else if (feature == "log_return") {
        for (size_t i = 1; i < n; ++i) {
            if (closes[i - 1] > 0 && closes[i] > 0) out[i] = std::log(closes[i] / closes[i - 1]);
        }
    } else if (feature == "volatility_20d") {
        for (size_t i = 20; i < n; ++i) {
            double sum = 0.0, sum_sq = 0.0;
            int cnt = 0;
            for (size_t k = i - 19; k <= i; ++k) {
                if (closes[k - 1] <= 0) continue;
                const double r = (closes[k] - closes[k - 1]) / closes[k - 1];
                sum += r;
                sum_sq += r * r;
                ++cnt;
            }
            if (cnt < 20) continue;
            const double mean = sum / cnt;
            out[i] = std::sqrt(std::max(0.0, sum_sq / cnt - mean * mean));
        }
    } else if (feature == "volume_ratio") {
        const size_t m = std::min(n, volumes.size());
        for (size_t i = 6; i < m; ++i) {
            double avg = 0.0;
            for (size_t k = i - 5; k < i; ++k) avg += volumes[k];
            avg /= 5.0;
            out[i] = avg > 0 ? volumes[i] / avg : kNaN;
        }
    }
    return series;
}

}  // namespace

bool HMMHandler::buildObservations(const nlohmann::json& params,
                                   Eigen::MatrixXd& out_obs,
                                   Vector<String>& out_dates,
                                   Vector<String>& out_featureNames,
                                   Vector<String>& out_symbols,
                                   String& out_error) {
    auto symbols = params.value("symbols", std::vector<String>{});
    auto features = params.value("features", std::vector<String>{});

    const String start = params.value("start", "");
    const String end = params.value("end", "");
    const BarFreq targetFreq = parseFreq(params.value("freq", "Day"));

    static const std::set<String> kKnownFeatures = {
        "return_1d", "log_return", "volatility_20d", "volume_ratio"};
    // 不显式拒绝空特征：后续每个特征都不产出任何值，最终报出的
    // "no symbol produced usable data" 会把问题指向标的，误导排查方向。
    if (features.empty()) {
        out_error = "features is empty";
        return false;
    }
    for (const auto& f : features) {
        if (!kKnownFeatures.count(f)) {
            out_error = "unknown feature: " + f;
            return false;
        }
    }

    // 逐标的算特征，收集 (dateKey -> {symbol.feature -> value})，再做日期交集
    Map<String, Map<String, double>> rows;
    Vector<String> usedSymbols;

    for (const auto& sym : symbols) {
        symbol_t symT = to_symbol(toInternalSymbol(sym));
        Vector<String> barDates;
        // 一次查询同时取 close/volume，保证两者行数与日期完全对齐
        auto data = LoadHistoryDataWithFreq(symT, {"close", "volume"}, start, end,
                                           targetFreq, AdjType::HFQ, &barDates);
        auto closeIt = data.find("close");
        if (closeIt == data.end() || closeIt->second.size() < 30) {
            WARN("[HMM] insufficient data for {} ({} bars)", sym, closeIt == data.end() ? 0 : closeIt->second.size());
            continue;
        }
        const Vector<double>& closes = closeIt->second;
        Vector<double> volumes;
        auto volIt = data.find("volume");
        if (volIt != data.end()) volumes = volIt->second;

        Map<String, Map<String, double>> symRows;
        for (const auto& f : features) {
            auto series = computeFeatures(f, closes, volumes);
            const auto& vals = series[f];
            for (size_t i = 0; i < vals.size() && i < barDates.size(); ++i) {
                if (std::isnan(vals[i])) continue;
                symRows[dateKey(barDates[i])][sym + "." + f] = vals[i];
            }
        }
        if (symRows.empty()) continue;

        // 与已有行取交集：每个 dateKey 必须覆盖此前所有标的的 (symbol, feature)
        for (const auto& [dk, valMap] : symRows) {
            auto it = rows.find(dk);
            if (it == rows.end()) {
                rows.emplace(dk, valMap);
            } else {
                for (const auto& [k, v] : valMap) it->second[k] = v;
            }
        }
        usedSymbols.push_back(sym);
    }

    if (usedSymbols.empty()) {
        out_error = "no symbol produced usable data";
        return false;
    }

    Vector<String> featureNames;
    for (const auto& sym : usedSymbols) {
        for (const auto& f : features) featureNames.push_back(sym + "." + f);
    }

    // 只保留覆盖全部 (symbol, feature) 的日期
    Vector<String> dates;
    for (const auto& [dk, valMap] : rows) {
        if (valMap.size() == featureNames.size()) dates.push_back(dk);
    }
    if (dates.size() < 30) {
        out_error = "insufficient aligned observations: " + std::to_string(dates.size());
        return false;
    }
    std::sort(dates.begin(), dates.end());

    out_obs.resize(static_cast<Eigen::Index>(dates.size()), static_cast<Eigen::Index>(featureNames.size()));
    for (Eigen::Index r = 0; r < out_obs.rows(); ++r) {
        const auto& valMap = rows.at(dates[r]);
        for (Eigen::Index c = 0; c < out_obs.cols(); ++c) {
            auto it = valMap.find(featureNames[c]);
            out_obs(r, c) = (it == valMap.end()) ? kNaN : it->second;
        }
    }

    out_dates = std::move(dates);
    out_featureNames = std::move(featureNames);
    out_symbols = std::move(usedSymbols);
    return true;
}

void HMMHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        const auto action = req.get_param_value("action");
        if (action != "list") {
            res.status = 400;
            res.set_content(R"({"error": "GET supports action=list only"})", "application/json");
            return;
        }
        handleList(res);
    } catch (const std::exception& e) {
        WARN("[HMMHandler] get error: {}", e.what());
        res.status = 500;
        res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
    }
}

void HMMHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error& e) {
        // body 不是合法 JSON 属于请求错误，不是服务端故障
        res.status = 400;
        res.set_content(nlohmann::json({{"error", std::string("invalid JSON body: ") + e.what()}}).dump(),
                        "application/json");
        return;
    }

    try {
        const auto action = body.value("action", "");
        if (action == "train")         handleTrain(body, res);
        else if (action == "predict")  handlePredict(body, res);
        else if (action == "decode")   handleDecode(body, res);
        else if (action == "publish")  handlePublish(body, res);
        else {
            res.status = 400;
            res.set_content(R"({"error": "action must be one of train/predict/decode/publish"})", "application/json");
        }
    } catch (const nlohmann::json::type_error& e) {
        // 字段类型不对（如 features 传字符串）同样是请求错误
        res.status = 400;
        res.set_content(nlohmann::json({{"error", std::string("invalid parameter type: ") + e.what()}}).dump(),
                        "application/json");
    } catch (const std::exception& e) {
        WARN("[HMMHandler] post error: {}", e.what());
        res.status = 500;
        res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
    }
}

void HMMHandler::del(const httplib::Request& req, httplib::Response& res) {
    try {
        const auto modelId = req.get_param_value("model_id");
        const auto strategyId = req.get_param_value("strategy_id");
        if (!isSafeName(modelId) || !isSafeName(strategyId)) {
            res.status = 400;
            res.set_content(R"json({"error": "strategy_id and model_id are required (alnum, _ and - only)"})json", "application/json");
            return;
        }

        const String dir = experimentsDir(_server->GetConfig().GetDatabasePath());
        const String stem = strategyId + "_" + modelId;
        const String modelPath = (fs::path(dir) / (stem + ".json")).string();
        const String metaPath = (fs::path(dir) / (stem + ".meta.json")).string();
        if (!fs::exists(modelPath)) {
            res.status = 404;
            res.set_content(nlohmann::json({{"error", "model not found: " + stem}}).dump(), "application/json");
            return;
        }
        fs::remove(modelPath);
        if (fs::exists(metaPath)) fs::remove(metaPath);

        res.set_content(nlohmann::json({{"status", "success"}, {"deleted", stem}}).dump(), "application/json");
    } catch (const std::exception& e) {
        WARN("[HMMHandler] del error: {}", e.what());
        res.status = 500;
        res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
    }
}

void HMMHandler::handleTrain(const nlohmann::json& params, httplib::Response& res) {
    const auto symbols = params.value("symbols", std::vector<String>{});
    if (symbols.empty()) {
        res.status = 400;
        res.set_content(R"({"error": "symbols is empty"})", "application/json");
        return;
    }

    const int nStates = params.value("n_states", 3);
    if (nStates < 2 || nStates > 8) {
        res.status = 400;
        res.set_content(R"({"error": "n_states must be in [2, 8]"})", "application/json");
        return;
    }

    const String strategyId = params.value("strategy_id", "default");
    if (!isSafeName(strategyId)) {
        res.status = 400;
        res.set_content(R"({"error": "strategy_id must be alnum, _ or - only"})", "application/json");
        return;
    }

    Eigen::MatrixXd obs;
    Vector<String> dates, featureNames, usedSymbols;
    String buildError;
    if (!buildObservations(params, obs, dates, featureNames, usedSymbols, buildError)) {
        res.status = 400;
        res.set_content(nlohmann::json({{"error", buildError}}).dump(), "application/json");
        return;
    }

    GaussianHMM::Config cfg;
    cfg.n_states = nStates;
    cfg.n_features = static_cast<int>(featureNames.size());
    cfg.max_iter = params.value("max_iter", 100);
    cfg.tol = params.value("tol", 1e-4);
    cfg.regularization = params.value("regularization", 1e-6);
    cfg.random_seed = static_cast<uint32_t>(params.value("random_seed", 42));

    GaussianHMM hmm(cfg);
    const bool converged = hmm.train(obs);
    if (!hmm.is_trained()) {
        res.status = 500;
        res.set_content(nlohmann::json({{"error", "HMM training failed"}}).dump(), "application/json");
        return;
    }

    // to_json() 产出 model_config / model_params，正是 HMMNode::from_json 读的结构
    nlohmann::json modelJson = hmm.to_json();
    modelJson["model_id"] = ToString(Now(), "%Y%m%d_%H%M%S");
    modelJson["strategy_id"] = strategyId;
    modelJson["created_at"] = ToString(Now(), "%Y-%m-%dT%H:%M:%S");
    modelJson["features"] = featureNames;
    modelJson["symbols"] = usedSymbols;
    modelJson["n_states"] = nStates;   // 顶层冗余一份，供 list/meta 直接读取
    modelJson["n_features"] = cfg.n_features;
    modelJson["converged"] = converged;  // to_json 里的 converged 实际写的是 trained_，此处用真实收敛标志

    const String dir = experimentsDir(_server->GetConfig().GetDatabasePath());
    ensureDir(dir);
    const String stem = strategyId + "_" + modelJson["model_id"].get<String>();
    const String modelPath = (fs::path(dir) / (stem + ".json")).string();
    const String metaPath = (fs::path(dir) / (stem + ".meta.json")).string();

    {
        std::ofstream ofs(modelPath);
        if (!ofs.is_open()) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", "cannot write " + modelPath}}).dump(), "application/json");
            return;
        }
        ofs << modelJson.dump(2);
    }

    nlohmann::json meta = {
        {"model_id", modelJson["model_id"]},
        {"strategy_id", strategyId},
        {"created_at", modelJson["created_at"]},
        {"n_states", nStates},
        {"n_features", cfg.n_features},
        {"features", featureNames},
        {"symbols", usedSymbols},
        {"converged", converged},
        {"log_likelihood", hmm.log_likelihood()},
        {"data_points", dates.size()},
        {"date_start", dates.front()},
        {"date_end", dates.back()},
        {"training_params", {cfg.max_iter, cfg.tol, cfg.regularization, cfg.random_seed}},
        {"model_file", stem + ".json"},
    };
    {
        std::ofstream ofs(metaPath);
        if (ofs.is_open()) ofs << meta.dump(2);
    }

    auto dur = hmm.state_duration();
    res.set_content(nlohmann::json({
        {"status", "success"},
        {"model_id", modelJson["model_id"]},
        {"strategy_id", strategyId},
        {"model_path", modelPath},
        {"meta_path", metaPath},
        {"model", {
            {"n_states", nStates},
            {"n_features", cfg.n_features},
            {"pi", modelJson["model_params"]["pi"]},
            {"A", modelJson["model_params"]["A"]},
            {"mu", modelJson["model_params"]["mu"]},
            {"cov_diag", modelJson["model_params"]["cov_diag"]},
            {"state_duration", std::vector<double>(dur.data(), dur.data() + dur.size())},
            {"current_state", hmm.current_state()},
        }},
        {"training_info", {
            {"converged", converged},
            {"log_likelihood", hmm.log_likelihood()},
            {"data_points", dates.size()},
            {"features", featureNames},
            {"symbols", usedSymbols},
            {"date_start", dates.front()},
            {"date_end", dates.back()},
        }},
    }).dump(), "application/json");
}

void HMMHandler::handlePredict(const nlohmann::json& params, httplib::Response& res) {
    const auto modelPath = params.value("model_path", "");
    const auto observation = params.value("observation", std::vector<double>{});
    if (modelPath.empty() || observation.empty()) {
        res.status = 400;
        res.set_content(R"({"error": "model_path and observation are required"})", "application/json");
        return;
    }

    std::ifstream ifs(modelPath);
    if (!ifs.is_open()) {
        res.status = 404;
        res.set_content(nlohmann::json({{"error", "model file not found: " + modelPath}}).dump(), "application/json");
        return;
    }
    nlohmann::json modelJson;
    ifs >> modelJson;

    const int nFeatures = modelJson["model_config"]["n_features"];
    // 不校验会让 emission_log_prob 越界读 mu_(j, d)
    if (static_cast<int>(observation.size()) != nFeatures) {
        res.status = 400;
        res.set_content(nlohmann::json({
            {"error", "observation dimension mismatch"},
            {"expected", nFeatures},
            {"got", observation.size()}}).dump(), "application/json");
        return;
    }

    GaussianHMM hmm = GaussianHMM::from_json(modelJson);
    Eigen::VectorXd obs(observation.size());
    for (size_t i = 0; i < observation.size(); ++i) obs(i) = observation[i];

    Eigen::VectorXd probs = hmm.predict_proba(obs);
    res.set_content(nlohmann::json({
        {"status", "success"},
        {"prediction", {
            {"state_probs", std::vector<double>(probs.data(), probs.data() + probs.size())},
            {"most_likely_state", hmm.current_state()},
            {"log_likelihood", hmm.log_likelihood()},
        }},
    }).dump(), "application/json");
}

void HMMHandler::handleDecode(const nlohmann::json& params, httplib::Response& res) {
    const auto modelPath = params.value("model_path", "");
    if (modelPath.empty()) {
        res.status = 400;
        res.set_content(R"({"error": "model_path is required"})", "application/json");
        return;
    }

    std::ifstream ifs(modelPath);
    if (!ifs.is_open()) {
        res.status = 404;
        res.set_content(nlohmann::json({{"error", "model file not found: " + modelPath}}).dump(), "application/json");
        return;
    }
    nlohmann::json modelJson;
    ifs >> modelJson;

    // 沿用训练时的特征，否则多特征模型解码时维度对不上。
    // 模型里没有 features 字段时不能默认成 return_1d：特征语义与训练时不一致，
    // 解码出的状态序列是错的，且不会报任何错。
    // 模型存的是 "symbol.feature" 限定名，要还原成 buildObservations 认的裸名。
    const auto& modelFeatures = modelJson["features"];
    if (!modelFeatures.is_array() || modelFeatures.empty()) {
        res.status = 400;
        res.set_content(R"({"error": "model has no features field; retrain the model before decoding"})", "application/json");
        return;
    }
    Vector<String> bareFeatures =
        stripSymbolPrefix(modelFeatures.get<Vector<String>>());
    if (bareFeatures.empty()) {
        res.status = 400;
        res.set_content(R"({"error": "model features field is malformed"})", "application/json");
        return;
    }

    nlohmann::json decodeParams = params;
    decodeParams["features"] = bareFeatures;
    if (!params.contains("symbols")) {
        decodeParams["symbols"] = modelJson.value("symbols", std::vector<String>{});
    }

    Eigen::MatrixXd obs;
    Vector<String> dates, featureNames, usedSymbols;
    String buildError;
    if (!buildObservations(decodeParams, obs, dates, featureNames, usedSymbols, buildError)) {
        res.status = 400;
        res.set_content(nlohmann::json({{"error", buildError}}).dump(), "application/json");
        return;
    }
    if (obs.cols() != modelJson["model_config"]["n_features"]) {
        res.status = 400;
        res.set_content(nlohmann::json({
            {"error", "observation dimension does not match model"},
            {"model_n_features", modelJson["model_config"]["n_features"]},
            {"data_n_features", obs.cols()}}).dump(), "application/json");
        return;
    }

    GaussianHMM hmm = GaussianHMM::from_json(modelJson);
    const auto path = hmm.decode(obs);

    nlohmann::json transitions = nlohmann::json::array();
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] != path[i - 1]) {
            transitions.push_back({{"date", dates[i]}, {"from", path[i - 1]}, {"to", path[i]}});
        }
    }

    // std::map<int,int> 无法序列化成 json object（key 非 string），手工转
    nlohmann::json summary = nlohmann::json::object();
    for (int s = 0; s < hmm.config().n_states; ++s) {
        size_t cnt = 0;
        for (int v : path) if (v == s) ++cnt;
        summary[std::to_string(s)] = cnt;
    }

    res.set_content(nlohmann::json({
        {"status", "success"},
        {"decode", {
            {"state_sequence", std::vector<int>(path.begin(), path.end())},
            {"dates", dates},
            {"transitions", transitions},
            {"regime_summary", summary},
            {"n_observations", path.size()},
        }},
    }).dump(), "application/json");
}

void HMMHandler::handleList(httplib::Response& res) {
    const String dbPath = _server->GetConfig().GetDatabasePath();
    nlohmann::json result = {{"status", "success"}, {"experiments", nlohmann::json::array()}, {"production", nlohmann::json::array()}};

    for (const auto& [dir, key] : {std::pair{experimentsDir(dbPath), "experiments"},
                                   std::pair{productionDir(dbPath), "production"}}) {
        if (!fs::exists(dir)) continue;
        for (const auto& entry : fs::directory_iterator(dir)) {
            const String fname = entry.path().filename().string();
            if (fname.size() < 10 || fname.compare(fname.size() - 10, 10, ".meta.json") != 0) continue;
            std::ifstream ifs(entry.path());
            if (!ifs.is_open()) continue;
            nlohmann::json meta;
            try {
                ifs >> meta;
            } catch (...) {
                continue;
            }
            meta["meta_file"] = fname;
            result[key].push_back(meta);
        }
    }
    res.set_content(result.dump(), "application/json");
}

void HMMHandler::handlePublish(const nlohmann::json& params, httplib::Response& res) {
    const auto modelId = params.value("model_id", "");
    const auto strategyId = params.value("strategy_id", "");
    if (!isSafeName(modelId) || !isSafeName(strategyId)) {
        res.status = 400;
        res.set_content(R"json({"error": "strategy_id and model_id are required (alnum, _ and - only)"})json", "application/json");
        return;
    }

    const String dbPath = _server->GetConfig().GetDatabasePath();
    const String expDir = experimentsDir(dbPath);
    const String stem = strategyId + "_" + modelId;
    const String expModel = (fs::path(expDir) / (stem + ".json")).string();
    const String expMeta = (fs::path(expDir) / (stem + ".meta.json")).string();
    if (!fs::exists(expModel)) {
        res.status = 404;
        res.set_content(nlohmann::json({{"error", "experiment not found: " + stem}}).dump(), "application/json");
        return;
    }

    const String prodDir = productionDir(dbPath);
    ensureDir(prodDir);
    const String prodModel = (fs::path(prodDir) / (strategyId + ".json")).string();
    const String prodMeta = (fs::path(prodDir) / (strategyId + ".meta.json")).string();

    std::error_code ec;
    fs::copy_file(expModel, prodModel, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        res.status = 500;
        res.set_content(nlohmann::json({{"error", "copy failed: " + ec.message()}}).dump(), "application/json");
        return;
    }

    nlohmann::json meta;
    if (fs::exists(expMeta)) {
        std::ifstream ifs(expMeta);
        if (ifs.is_open()) try { ifs >> meta; } catch (...) {}
    }
    meta["published_at"] = ToString(Now(), "%Y-%m-%dT%H:%M:%S");
    meta["source"] = "experiment";
    meta["source_model_id"] = modelId;
    meta["model_file"] = strategyId + ".json";
    {
        std::ofstream ofs(prodMeta);
        if (ofs.is_open()) ofs << meta.dump(2);
    }

    // 发布后的路径直接给前端填 HMMNode 的 modelFile
    res.set_content(nlohmann::json({
        {"status", "success"},
        {"strategy_id", strategyId},
        {"production_path", prodModel},
        {"meta_path", prodMeta},
    }).dump(), "application/json");
}