#pragma once
#include "std_header.h"
#include "json.hpp"
#include "httplib.h"
#include "Util/PythonRunner.h"
#include <atomic>
#include <condition_variable>
#include <mutex>

// 异步作业的事件会话（与业务无关）。
//
// 一次作业 = 一个 JobSession：
//   - 后台线程把事件 pushEvent() 进 _eventLog（预序列化 JSON）；
//   - SSE provider 从 _eventLog[0] 开始回放，之后每来一条推一帧；
//   - finish() 置 _done，provider 把剩余事件发完就关闭连接。
//
// 这样客户端在任务中途接入也能看到完整历史，断线重连只是重新订阅同一个 session。
// 训练 / 优化 / 特征筛选 / 稳定性扫描共用这一份，不要再各自实现事件日志。

/// 任务号：进程内自增的整数。前端靠它把 SSE 事件关联到自己的进度产物，
/// 也能用它查回任务状态（推送漏了的时候）。
using JobId = uint64_t;

struct JobEvent {
    String _type;
    String _dataStr;   // 已序列化的 JSON：回放时直接拼进 SSE 帧，不再 dump
};

struct JobSession {
    String _sessionId;

    std::atomic<bool> _done{false};
    std::atomic<bool> _cancelled{false};

    // 后台 Python 子进程句柄，cancel 时透过它 kill()。
    // 用 shared_ptr：cancel 线程调用 kill() 期间，后台线程可能已经跑完并释放自己的引用。
    std::shared_ptr<PythonRunner> _runner;

    mutable std::mutex _mtx;
    std::condition_variable _cv;

    Vector<JobEvent> _eventLog;
    nlohmann::json _result;      // 终态结果（finish 时写入）
    nlohmann::json _progress;    // 最近一次 progress 事件，轮询式进度条用
    nlohmann::json _meta;        // 静态元信息（freq/symbols），查询时原样返回
    bool _hasError = false;
    long long _finishedAtMs = 0; // 终态时间（毫秒）；0 = 还在跑

    // 线程安全地推送事件。type=="progress" 时顺手更新 _progress。
    void pushEvent(const String& type, const nlohmann::json& data);

    // 设置终态并唤醒所有等待者。
    void finish(const nlohmann::json& res, bool error = false);

    // 置取消位并 kill 子进程（没有 runner 时只置位）。
    void cancel();

    // 会话元信息：查询快照时原样返回，不参与事件回放。
    void setMeta(const nlohmann::json& meta);

    // 终态会话是否已过期（还在跑的永远不过期）
    bool expired(long long ttlMs, long long nowMs) const;

    // 状态快照：{"status":"running|done|error"} + progress / 结果字段。
    // includeResult=false 时只给 status，用于只要状态的场景。
    nlohmann::json snapshot(bool includeResult = true);
};

// 会话登记表：session_id → 会话
//
// JobSession 只保证「连上 SSE 之后能回放」。客户端没连上时（后端重启、
// 事件发得太早、页面刷新）推送这条路是断的 —— 状态必须可查，不能只靠赶上。
//
// 训练/优化一次只跑一个，用各自的 _activeSession 就够；数据下载并发多条、
// 前端还要按 job_id 关联，才需要一张按 id 查的表。
class JobSessionStore {
public:
    // 同 id 已存在时返回已有的 —— 已在跑的任务不能被顶掉
    std::shared_ptr<JobSession> Create(JobId id);

    // 查不到返回 nullptr
    std::shared_ptr<JobSession> Find(JobId id);

    // 移除一个会话（任务没派发出去时用：查得到却永远不会有结果更糟）
    void Remove(JobId id);

    // 清掉过期的终态会话
    void Prune();

    // 终态会话保留时长（毫秒）：迟到的客户端还能查到自己那个任务的结果
    static constexpr long long kTtlMs = 30 * 60 * 1000;

private:
    void pruneLocked();

    mutable std::mutex _mtx;
    std::map<JobId, std::shared_ptr<JobSession>> _sessions;
};

// 任务派发结果
//
// 同一个标的只允许一个任务在下载：派发时按标的粒度判重，
// 已在跑的标的不再下一遍 —— 两个任务写同一个 CSV、入同一张表会打架。
struct DataJobDispatch {
    bool _dispatched = false;      ///< 是否真的派了新任务
    JobId _jobId = 0;              ///< 生效的 job_id（复用时是在跑那个任务的）
    Set<symbol_t> _skipped;        ///< 已在别的任务里下载、本次没下的标的
    Set<symbol_t> _dispatching;    ///< 本次真正要下的标的
};

// ML 任务类型：用于会话幂等检查，不同 kind 的任务不能共享同一个 session
enum class MLAction : uint8_t {
    Unknown = 0,
    Train,
    Optimize,
    Collect,
    Screen,
};

struct TrainSession : JobSession {
    MLAction _kind = MLAction::Unknown;  // 会话归属的 action，用于幂等检查

    // 特征收集阶段产出（collect 完成后设置，train 可复用）
    String _csvPath;
    nlohmann::json _featureStats;
};

// 会话获取结果：用于幂等检查。模板参数为具体的 Session 类型（TrainSession / StabilitySession ...）。
template <class SessionT>
struct SessionAcquireResult {
    enum Status {
        CanCreate,   // 可以创建新会话（无活跃会话或活跃会话已 done）
        Reused,      // 同 kind 的活跃会话存在，可复用
        Conflict     // 不同 kind 的活跃会话存在，冲突
    };
    Status status;
    std::shared_ptr<SessionT> existing;  // Reused 或 Conflict 时非空
};

// ---- SSE 公共部分 ----
// 四个 SSE 端点（/python/run、/sr/run、/server/event、/ml）原先各写一遍的响应头。
void SetSseHeaders(httplib::Response& res);

// 组装一帧不带 id 的 SSE：event:<type>\ndata:<json>\n\n
String SseFrame(const String& type, const String& dataJson);

// 回放 JobSession 的事件日志，之后实时推送；session 结束后关闭连接。
void ServeJobSession(httplib::Response& res,
                     std::shared_ptr<JobSession> session,
                     int poll_ms = 500);

// 把一个已启动的 PythonRunner 的 stdout/stderr 直接流成 SSE（无历史回放）。
// runner 必须是 shared_ptr：httplib 在 handler 返回之后才调用 content provider，
// 按引用捕获栈上的 runner 会 use-after-free。
void ServePythonRunner(httplib::Response& res,
                       std::shared_ptr<PythonRunner> runner,
                       int read_timeout_ms,
                       const String& stdoutEvent = "output");
