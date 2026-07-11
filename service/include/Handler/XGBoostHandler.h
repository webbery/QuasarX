#pragma once
#include "HttpHandler.h"
#include <xgboost/c_api.h>
#include <xgboost/version_config.h>
#include <atomic>
#include <mutex>

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

// 单一端点：POST /v0/xgboost
// 通过 body 中的 "action" 字段路由：
//   action=train   训练模型（返回完整结果 JSON）
//   action=shap    计算 SHAP 值
//   action=delete  释放内存中已注册的模型
class XGBoostHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;

    void post(const httplib::Request& req, httplib::Response& res) override;

    // 注册模型到缓存（SHAP 计算用）
    uint64_t registerModel(BoosterHandle booster, Vector<String> features, Vector<Vector<double>> x_test);
    CachedXGBoostModel* getModel(uint64_t id);
    bool deleteModel(uint64_t id);

private:
    Map<uint64_t, CachedXGBoostModel> _cache;
    std::atomic<uint64_t> _nextId{1};
    std::mutex _mtx;
};
