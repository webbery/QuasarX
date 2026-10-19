#include "Handler/ServerEventHandler.h"
#include "Util/system.h"
#include "nng/nng.h"
#include "server.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>

// SSE 订阅 socket 的接收超时。nng_recv 到点返回 NNG_ETIMEDOUT，provider 借此回到循环
// 顶部重新检查 sink.is_writable() —— 这个值就是「客户端断开后多久能感知到」的上限。
// 空载连接每秒醒一次，客户端数量不多时代价可忽略。
static constexpr short SSE_RECV_TIMEOUT_MS = 1000;

// 空闲心跳间隔。必须有心跳，否则空闲连接一次 socket 写入都不会发生，
// 服务端永远发现不了对端已经走了 —— 僵尸订阅者只增不减，各自占着一个 nng socket
// 和一个 httplib worker 线程，worker 池迟早被占满连带其他 API 一起卡住。
static constexpr auto SSE_HEARTBEAT_INTERVAL = std::chrono::seconds(15);

// SSE 注释行。冒号开头的行会被前端解析器识别成 fieldLength == 0 而直接忽略，
// 不会产生消息。注意结尾只留一个换行、不要再加空行：空行会触发一次「空消息派发」
// （虽然被 SSEService 的 if (event.data) 挡掉，但每次心跳都会白跑两行 console.log）。
static constexpr std::string_view SSE_HEARTBEAT = ": keepalive\n";

namespace {

/// 每条 SSE 连接一份的订阅状态。
///
/// 刻意不用 thread_local：nng_close 按值传参（int nng_close(nng_socket)），
/// 关不掉调用方的 sock.id。挂在 worker 线程上会导致「下一条落到同线程的连接」
/// 看到 sock.id != 0 而跳过 Subscribe()，直接对已关闭的 socket 调 nng_recv，
/// 立刻失败 → return true → httplib 反复回调 → 空转烧核，且该连接一个事件都收不到。
struct SseConn {
    nng_socket sock{0};
    uint32_t seq{0};
    std::chrono::steady_clock::time_point next_heartbeat{};

    ~SseConn() {
        close();
    }

    void close() {
        if (sock.id) {
            nng_close(sock);
            sock.id = 0;
        }
    }
};

std::atomic<uint32_t> g_sse_conn_seq{0};

}  // namespace

void ServerEventHandler::run()
{
    // 收集消息，然后分发给每个sink
    nng_socket pull_sock, pub_sock;
    if (!Puller(URI_SERVER_EVENT, pull_sock)) {
        WARN("[SSE] dispatcher: listen on {} failed, SSE fan-out disabled", URI_SERVER_EVENT);
        return;
    }
    if (!Publish(URI_DISPATH_EVENT, pub_sock)) {
        WARN("[SSE] dispatcher: publish on {} failed, close pull socket {}", URI_DISPATH_EVENT, URI_SERVER_EVENT);
        nng_close(pull_sock);
        return;
    }
    SetCurrentThreadName("SSEDispather");
    INFO("[SSE] dispatcher started: {} -> {}", URI_SERVER_EVENT, URI_DISPATH_EVENT);
    while (!Server::IsExit()) {
        char* buff = NULL;
        size_t sz = 0;
        int rv = nng_recv(pull_sock, &buff, &sz, NNG_FLAG_ALLOC);
        if (rv != 0) {
            if (buff) {
                nng_free(buff, sz);
            }
            WARN("[SSE] dispatcher: nng_recv on {} failed: {}", URI_SERVER_EVENT, nng_strerror(rv));
            continue;
        }
        // 发送给客户端
        if (nng_send(pub_sock, buff, sz, NNG_FLAG_NONBLOCK) != 0) {
            WARN("[SSE] dispatcher: nng_send on {} failed, {} bytes dropped", URI_DISPATH_EVENT, sz);
        }
        nng_free(buff, sz);
    }
    nng_close(pull_sock);
    nng_close(pub_sock);
    INFO("[SSE] dispatcher stopped");
}

std::thread* ServerEventHandler::_dispather = nullptr;

ServerEventHandler::ServerEventHandler(Server* server):HttpHandler(server) {
}

ServerEventHandler::~ServerEventHandler() {
    if (_dispather) {
        delete _dispather;
    }
}

void ServerEventHandler::get(const httplib::Request& req, httplib::Response& res) {
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*"); // 允许跨域
    if (!_dispather) {
        _dispather = new std::thread(&ServerEventHandler::run, this);
    }

    // 每个 get() 一份 conn，lambda 按值捕获 —— 订阅状态随连接走，不随线程走
    auto conn = std::make_shared<SseConn>();
    conn->seq = ++g_sse_conn_seq;

    res.set_chunked_content_provider("text/event-stream",
    [conn](size_t offset, httplib::DataSink& sink) {
        // 当客户端首次连接时，将其添加到分发器
        if (conn->sock.id == 0) {
            if (!Subscribe(URI_DISPATH_EVENT, conn->sock, SSE_RECV_TIMEOUT_MS)) {
                // Subscribe 失败时可能已打开 socket，交给 ~SseConn 兜底关闭
                WARN("[SSE] conn#{}: subscribe {} failed, close connection",
                     conn->seq, URI_DISPATH_EVENT);
                return false;
            }
            conn->next_heartbeat = std::chrono::steady_clock::now() + SSE_HEARTBEAT_INTERVAL;
            INFO("[SSE] conn#{}: subscribed {}, recv-timeout {}ms, heartbeat {}s",
                 conn->seq, URI_DISPATH_EVENT, (int)SSE_RECV_TIMEOUT_MS,
                 (int)std::chrono::duration_cast<std::chrono::seconds>(SSE_HEARTBEAT_INTERVAL).count());
        }
        if (!sink.is_writable() || Server::IsExit()) {
            INFO("[SSE] conn#{}: {}, close socket",
                 conn->seq, Server::IsExit() ? "server exiting" : "client disconnected");
            conn->close();
            sink.done();
            return false;
        }
        char* buff = NULL;
        size_t sz = 0;
        int rv = nng_recv(conn->sock, &buff, &sz, NNG_FLAG_ALLOC);
        if (rv == NNG_ETIMEDOUT) {
            // 正常心跳：没有新事件。顺带按间隔发一条 SSE 注释行 ——
            // 这是空闲连接唯一的 socket 写入机会，写失败即说明对端已走，立刻回收。
            auto now = std::chrono::steady_clock::now();
            if (now >= conn->next_heartbeat) {
                conn->next_heartbeat = now + SSE_HEARTBEAT_INTERVAL;
                if (!sink.write(SSE_HEARTBEAT.data(), SSE_HEARTBEAT.size())) {
                    WARN("[SSE] conn#{}: heartbeat write failed, close connection", conn->seq);
                    conn->close();
                    return false;
                }
            }
            // 回到循环顶部重新检查 sink 是否还活着
            return true;
        }
        if (rv != 0) {
            if (buff) {
                nng_free(buff, sz);
            }
            WARN("[SSE] conn#{}: nng_recv failed: {}, close connection",
                 conn->seq, nng_strerror(rv));
            conn->close();
            return false;
        }
        // 发送消息
        bool ok = sink.write(buff, sz);
        nng_free(buff, sz);
        if (!ok) {
            WARN("[SSE] conn#{}: sink.write failed, close connection", conn->seq);
            conn->close();
        }
        return ok;
    });
    res.status = 200;
}
