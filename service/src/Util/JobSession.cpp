#include "Util/JobSession.h"
#include "Util/string_algorithm.h"
#include "server.h"
#include <chrono>

namespace {

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

void JobSession::pushEvent(const String& type, const nlohmann::json& data) {
    String dataStr;
    try {
        dataStr = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (...) {
        dataStr = "{}";
    }
    std::lock_guard<std::mutex> lk(_mtx);
    if (type == "progress") _progress = data;
    _eventLog.push_back({type, std::move(dataStr)});
    _cv.notify_all();
}

void JobSession::finish(const nlohmann::json& res, bool error) {
    std::lock_guard<std::mutex> lk(_mtx);
    _result = res;
    _hasError = error;
    _done = true;
    _finishedAtMs = NowMs();
    _cv.notify_all();
}

void JobSession::setMeta(const nlohmann::json& meta) {
    std::lock_guard<std::mutex> lk(_mtx);
    _meta = meta;
}

bool JobSession::expired(long long ttlMs, long long nowMs) const {
    std::lock_guard<std::mutex> lk(_mtx);
    if (!_done) return false;
    return _finishedAtMs > 0 && (nowMs - _finishedAtMs) > ttlMs;
}

void JobSession::cancel() {
    _cancelled = true;
    // 拷贝 shared_ptr：kill() 执行期间后台线程可能已经把 _runner 置空/析构
    std::shared_ptr<PythonRunner> runner;
    {
        std::lock_guard<std::mutex> lk(_mtx);
        runner = _runner;
    }
    if (runner) runner->kill();
}

nlohmann::json JobSession::snapshot(bool includeResult) {
    std::lock_guard<std::mutex> lk(_mtx);
    nlohmann::json resp;
    if (!_done) {
        resp["status"] = "running";
        if (!_progress.is_null()) resp["progress"] = _progress;
        if (!_meta.is_null()) resp["meta"] = _meta;
        return resp;
    }
    if (includeResult && _result.is_object()) {
        resp = _result;
    } else {
        resp = nlohmann::json::object();
    }
    resp["status"] = _hasError ? "error" : "done";
    if (!_meta.is_null()) resp["meta"] = _meta;
    return resp;
}

std::shared_ptr<JobSession> JobSessionStore::Create(JobId id) {
    std::lock_guard<std::mutex> lk(_mtx);
    pruneLocked();
    auto it = _sessions.find(id);
    if (it != _sessions.end()) return it->second;
    auto session = std::make_shared<JobSession>();
    session->_sessionId = std::to_string(id);
    _sessions[id] = session;
    return session;
}

std::shared_ptr<JobSession> JobSessionStore::Find(JobId id) {
    std::lock_guard<std::mutex> lk(_mtx);
    auto it = _sessions.find(id);
    return it == _sessions.end() ? nullptr : it->second;
}

void JobSessionStore::Remove(JobId id) {
    std::lock_guard<std::mutex> lk(_mtx);
    _sessions.erase(id);
}

void JobSessionStore::Prune() {
    std::lock_guard<std::mutex> lk(_mtx);
    pruneLocked();
}

void JobSessionStore::pruneLocked() {
    const long long now = NowMs();
    for (auto it = _sessions.begin(); it != _sessions.end();) {
        if (it->second->expired(kTtlMs, now)) {
            it = _sessions.erase(it);
        } else {
            ++it;
        }
    }
}

void SetSseHeaders(httplib::Response& res) {
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*");
}

String SseFrame(const String& type, const String& dataJson) {
    String msg = "event:" + type + "\ndata:" + dataJson + "\n\n";
    return msg;
}

void ServeJobSession(httplib::Response& res,
                     std::shared_ptr<JobSession> session,
                     int poll_ms) {
    SetSseHeaders(res);
    size_t replayIdx = 0;
    res.set_chunked_content_provider("text/event-stream",
        [session, replayIdx, poll_ms](size_t /*offset*/, httplib::DataSink& sink) mutable -> bool {
            if (!sink.is_writable()) return false;

            while (true) {
                std::unique_lock<std::mutex> lk(session->_mtx);

                // 回放历史事件
                if (replayIdx < session->_eventLog.size()) {
                    auto& ev = session->_eventLog[replayIdx++];
                    String msg = SseFrame(ev._type, ev._dataStr);
                    lk.unlock();
                    sink.write(msg.c_str(), msg.size());
                    return true;
                }

                // 任务已结束：剩余事件都已发完，关闭连接
                if (session->_done) {
                    lk.unlock();
                    sink.done();
                    return false;
                }

                // 等待新事件
                session->_cv.wait_for(lk, std::chrono::milliseconds(poll_ms));
            }
        });
}

void ServePythonRunner(httplib::Response& res,
                       std::shared_ptr<PythonRunner> runner,
                       int read_timeout_ms,
                       const String& stdoutEvent) {
    SetSseHeaders(res);
    res.set_chunked_content_provider("text/event-stream",
        [runner, read_timeout_ms, stdoutEvent](size_t /*offset*/, httplib::DataSink& sink) -> bool {
            if (!sink.is_writable() || Server::IsExit()) {
                runner->kill();
                sink.done();
                return false;
            }

            PythonOutput out;
            if (runner->readLine(out, read_timeout_ms)) {
                if (out.type == PythonOutput::DONE) {
                    auto msg = format_sse("done",
                        {{"exit_code", std::to_string(out.exit_code)}});
                    sink.write(msg.c_str(), msg.size());
                    sink.done();
                    return false;
                }
                String eventType = (out.type == PythonOutput::STDOUT) ? stdoutEvent : "error";
                auto msg = format_sse(eventType, {{"line", out.line}});
                sink.write(msg.c_str(), msg.size());
            }
            return true;
        });
}
