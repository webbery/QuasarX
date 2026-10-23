#pragma once
#include "HttpHandler.h"
#include <xgboost/c_api.h>
#include <xgboost/version_config.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

class QNode;

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

// ML 任务类型：用于会话幂等检查，不同 kind 的任务不能共享同一个 session
enum class MLAction : uint8_t {
    Unknown = 0,
    Train,
    Optimize,
    Collect,
    Screen,
};

inline String mlActionToString(MLAction a) {
    switch (a) {
        case MLAction::Train:    return "train";
        case MLAction::Optimize: return "optimize";
        case MLAction::Collect:  return "collect";
        case MLAction::Screen:   return "screen";
        default: return "unknown";
    }
}

inline MLAction stringToMLAction(const String& s) {
    if (s == "train")    return MLAction::Train;
    if (s == "optimize") return MLAction::Optimize;
    if (s == "collect")  return MLAction::Collect;
    if (s == "screen")   return MLAction::Screen;
    return MLAction::Unknown;
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
class PythonRunner;  // forward declaration

struct TrainSession {
    String _sessionId;
    MLAction _kind = MLAction::Unknown;  // 会话归属的 action，用于幂等检查
    std::atomic<bool> _done{false};
    std::atomic<bool> _cancelled{false};
    
    // 后台 Python 进程句柄，用于 cancel 时终止子进程
    PythonRunner* _runner = nullptr;

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

// 会话获取结果：用于幂等检查
struct SessionAcquireResult {
    enum Status {
        CanCreate,   // 可以创建新会话（无活跃会话或活跃会话已 done）
        Reused,      // 同 kind 的活跃会话存在，可复用
        Conflict     // 不同 kind 的活跃会话存在，冲突
    };
    Status status;
    std::shared_ptr<TrainSession> existing;  // Reused 或 Conflict 时非空
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
    // 特征筛选门禁：吃 collect 产出的 CSV，跑 tools/feature_screen.py
    void handleScreen(const nlohmann::json& params, httplib::Response& res);
    // 读取一次筛选的完整结果 JSON（summary 见 train_status）
    void handleScreenResult(const httplib::Request& req, httplib::Response& res);
    void handleTrainProgress(const httplib::Request& req, httplib::Response& res);
    void handleTrainStatus(const httplib::Request& req, httplib::Response& res);
    void handleCancel(const httplib::Request& req, httplib::Response& res);
    void handleShap(const nlohmann::json& params, httplib::Response& res);
    void handleList(const httplib::Request& req, httplib::Response& res);
    void handleDelete(uint64_t modelId, httplib::Response& res);
    void handleUpdateMeta(const nlohmann::json& params, httplib::Response& res);

    // 尝试获取会话槽：检查当前活跃会话，返回 CanCreate/Reused/Conflict
    static SessionAcquireResult tryAcquireSession(MLAction kind);

    // 构建 409 冲突响应：当有其他 ML 会话正在运行时
    static void respondConflict(httplib::Response& res, const String& sessionId, MLAction kind);

private:

    Map<uint64_t, CachedMLModel> _cache;
    std::atomic<uint64_t> _nextId{1};
    std::mutex _mtx;

    // 训练会话管理
    static std::shared_ptr<TrainSession> s_activeSession;
    static std::mutex s_sessionMtx;
};
