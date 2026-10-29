#pragma once
#include "HttpHandler.h"
#include "Handler/MLHandler.h"
#include "Util/JobSession.h"
#include <mutex>

class QNode;

// 策略稳定性扫描任务类型
enum class StabilityAction : uint8_t {
    Unknown = 0,
    Scan,      // 启动扫描
    Cancel,    // 取消扫描
    Delete,    // 删除结果
};

inline String stabilityActionToString(StabilityAction a) {
    switch (a) {
        case StabilityAction::Scan:   return "scan";
        case StabilityAction::Cancel: return "cancel";
        case StabilityAction::Delete: return "delete";
        default:                      return "unknown";
    }
}

inline StabilityAction stringToStabilityAction(const String& s) {
    if (s == "scan")   return StabilityAction::Scan;
    if (s == "cancel") return StabilityAction::Cancel;
    if (s == "delete") return StabilityAction::Delete;
    return StabilityAction::Unknown;
}

// 稳定性扫描会话（继承 JobSession）
struct StabilitySession : JobSession {
    StabilityAction _kind = StabilityAction::Unknown;
    String _runId;
    String _csvPath;
    String _jsonOutPath;
};

class StabilityHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;

    void get(const httplib::Request& req, httplib::Response& res) override;
    void post(const httplib::Request& req, httplib::Response& res) override;

private:
    // POST 处理
    void handleScan(const nlohmann::json& params, httplib::Response& res);
    void handleCancel(const nlohmann::json& params, httplib::Response& res);
    void handleDelete(const nlohmann::json& params, httplib::Response& res);

    // GET 处理
    void handleEvents(const httplib::Request& req, httplib::Response& res);
    void handleStatus(const httplib::Request& req, httplib::Response& res);
    void handleResult(const httplib::Request& req, httplib::Response& res);
    void handleList(const httplib::Request& req, httplib::Response& res);

    // 会话管理
    static SessionAcquireResult<StabilitySession> tryAcquireSession(StabilityAction kind);
    static void respondConflict(httplib::Response& res, const String& sessionId, StabilityAction kind);

    // 单会话槽（同一时刻只允许一个扫描任务）
    static std::shared_ptr<StabilitySession> s_activeSession;
    static std::mutex s_sessionMtx;
};
