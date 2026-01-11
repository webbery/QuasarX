#include "Handler/ServerEventHandler.h"
#include "Util/system.h"
#include "nng/nng.h"
#include "server.h"
#include <thread>

void ServerEventHandler::run()
{
    // 收集消息，然后分发给每个sink
    nng_socket pull_sock, pub_sock;
    if (!Puller(URI_SERVER_EVENT, pull_sock)) {
        WARN("EventDispatch Subscribe fail.");
        return;
    }
    if (!Publish(URI_DISPATH_EVENT, pub_sock)) {
        WARN("EventDispatch Subscribe fail.");
        return;
    }
    SetCurrentThreadName("SSEDispather");
    while (!Server::IsExit()) {
        char* buff = NULL;
        size_t sz = 0;
        int rv = nng_recv(pull_sock, &buff, &sz, NNG_FLAG_ALLOC);
        if (rv != 0) {
            nng_free(buff, sz);
            continue;
        }
        // 发送给客户端
        nng_send(pub_sock, buff, sz, NNG_FLAG_NONBLOCK);
        nng_free(buff, sz);
    }
    nng_close(pull_sock);
    nng_close(pub_sock);
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
    res.set_chunked_content_provider("text/event-stream",
        [&](size_t offset, httplib::DataSink& sink) {
        auto id = std::this_thread::get_id();
        // 当客户端首次连接时，将其添加到分发器
        thread_local nng_socket sock{ 0 };
        if (sock.id == 0) {
            Subscribe(URI_DISPATH_EVENT, sock);
        }
        if (!sink.is_writable() || Server::IsExit()) {
            nng_close(sock);
            return false;
        }
        char* buff = NULL;
        size_t sz = 0;
        int rv = nng_recv(sock, &buff, &sz, NNG_FLAG_ALLOC);
        if (rv != 0) {
            nng_free(buff, sz);
            return true;
        }
        // 发送消息
        return sink.write(buff, sz);
    });
    res.status = 200;
}
