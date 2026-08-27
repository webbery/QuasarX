#pragma once
#include "HttpHandler.h"
#include <xgboost/c_api.h>
#include <xgboost/version_config.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

enum class ModelType : uint8_t {
    XGBoost = 0,
    NARX,
    // 预留：LightGBM, CatBoost, ...
};

inline String modelTypeToString(ModelType t) {
    switch (t) {
        case ModelType::XGBoost: return "xgboost";
        case ModelType::NARX:    return "narx";
        default: return "unknown";
    }
}

inline ModelType stringToModelType(const String& s) {
    if (s == "xgboost" || s == "xgb") return ModelType::XGBoost;
    if (s == "narx") return ModelType::NARX;
    return ModelType::XGBoost;  // 默认
}

struct CachedMLModel {
    ModelType _modelType = ModelType::XGBoost;
    BoosterHandle _booster = nullptr;
    Vector<String> _features;
    Eigen::MatrixXd _x_test;  // 行=样本, 列=特征
    Vector<String> _x_test_dates;  // 与 _x_test 行对齐的日期 (YYYY-MM-DD)
    String _modelPath;        // 实验文件路径，用于下载 / bind

    ~CachedMLModel() { clear(); }

    void clear() {
        if (_booster) {
            XGBoosterFree(_booster);
            _booster = nullptr;
        }
        _features.clear();
        _x_test_dates.clear();
        _modelPath.clear();
    }
};

// 训练会话：记录事件历史，支持 SSE 重连回放
struct TrainSession {
    String _sessionId;
    std::atomic<bool> _done{false};
    std::atomic<bool> _cancelled{false};

    std::mutex _mtx;
    std::condition_variable _cv;

    // 事件历史（用于重连后回放）
    // 存储预序列化的 JSON 字符串，避免 replay 时 dump 已损坏的 json 对象
    struct Event { String _type; String _dataStr; };
    Vector<Event> _eventLog;

    // 最终结果（训练完成后设置）
    nlohmann::json _result;
    bool _hasError = false;

    // 特征收集阶段产出（collect 完成后设置，train 可复用）
    String _csvPath;
    nlohmann::json _featureStats;

    // 线程安全地推送事件
    void pushEvent(const String& type, const nlohmann::json& data) {
        String dataStr;
        try {
            dataStr = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        } catch (...) {
            dataStr = "{}";
        }
        std::lock_guard<std::mutex> lk(_mtx);
        _eventLog.push_back({type, std::move(dataStr)});
        _cv.notify_all();
    }

    void finish(const nlohmann::json& res, bool error = false) {
        std::lock_guard<std::mutex> lk(_mtx);
        _result = res;
        _hasError = error;
        _done = true;
        _cv.notify_all();
    }
};

class MLHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;

    void get(const httplib::Request& req, httplib::Response& res) override;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;

    // 注册模型到缓存（SHAP 计算用）
    uint64_t registerModel(ModelType type, BoosterHandle booster, Vector<String> features, Eigen::MatrixXd x_test);
    CachedMLModel* getModel(uint64_t id);
    bool deleteModel(uint64_t id);

private:
    void handleTrain(const nlohmann::json& params, httplib::Response& res);
    void handleOptimize(const nlohmann::json& params, httplib::Response& res);
    void handleCollect(const nlohmann::json& params, httplib::Response& res);
    void handleTrainProgress(const httplib::Request& req, httplib::Response& res);
    void handleTrainStatus(const httplib::Request& req, httplib::Response& res);
    void handleShap(const nlohmann::json& params, httplib::Response& res);
    void handleList(const httplib::Request& req, httplib::Response& res);
    void handleDelete(uint64_t modelId, httplib::Response& res);

    Map<uint64_t, CachedMLModel> _cache;
    std::atomic<uint64_t> _nextId{1};
    std::mutex _mtx;

    // 训练会话管理
    static std::shared_ptr<TrainSession> s_activeSession;
    static std::mutex s_sessionMtx;
};
