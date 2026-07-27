#pragma once
#include "HttpHandler.h"
#include <xgboost/c_api.h>
#include <xgboost/version_config.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

struct CachedXGBoostModel {
    BoosterHandle booster = nullptr;
    Vector<String> features;
    Vector<Vector<double>> X_test;

    ~CachedXGBoostModel() { clear(); }

    void clear() {
        if (booster) {
            XGBoosterFree(booster);
            booster = nullptr;
        }
        features.clear();
        X_test.clear();
    }
};

// 训练会话：记录事件历史，支持 SSE 重连回放
struct TrainSession {
    String sessionId;
    std::atomic<bool> done{false};
    std::atomic<bool> cancelled{false};

    std::mutex mtx;
    std::condition_variable cv;

    // 事件历史（用于重连后回放）
    struct Event { String type; nlohmann::json data; };
    Vector<Event> eventLog;

    // 最终结果（训练完成后设置）
    nlohmann::json result;
    bool hasError = false;

    // 线程安全地推送事件
    void pushEvent(const String& type, const nlohmann::json& data) {
        std::lock_guard<std::mutex> lk(mtx);
        eventLog.push_back({type, data});
        cv.notify_all();
    }

    void finish(const nlohmann::json& res, bool error = false) {
        std::lock_guard<std::mutex> lk(mtx);
        result = res;
        hasError = error;
        done = true;
        cv.notify_all();
    }
};

class XGBoostHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;

    void get(const httplib::Request& req, httplib::Response& res) override;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;

    // 注册模型到缓存（SHAP 计算用）
    uint64_t registerModel(BoosterHandle booster, Vector<String> features, Vector<Vector<double>> x_test);
    CachedXGBoostModel* getModel(uint64_t id);
    bool deleteModel(uint64_t id);

private:
    void handleTrain(const nlohmann::json& params, httplib::Response& res);
    void handleTrainProgress(const httplib::Request& req, httplib::Response& res);
    void handleShap(const nlohmann::json& params, httplib::Response& res);
    void handlePublish(const nlohmann::json& params, httplib::Response& res);
    void handleList(httplib::Response& res);
    void handleDelete(uint64_t modelId, httplib::Response& res);

    Map<uint64_t, CachedXGBoostModel> _cache;
    std::atomic<uint64_t> _nextId{1};
    std::mutex _mtx;

    // 训练会话管理
    static std::shared_ptr<TrainSession> s_activeSession;
    static std::mutex s_sessionMtx;
};
