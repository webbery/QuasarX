#include "Handler/ServerEventHandler.h"
#include "Util/system.h"
#include "nng/nng.h"
#include "server.h"
#include <thread>
#include <unistd.h>

class EventDispatcher {
public:
    EventDispatcher() { _sock.id = 0; }
    ~EventDispatcher() {

    }

    bool dispatchEvent(httplib::DataSink& sink) {
        if (_sock.id == 0) {
            if (!Subscribe(URI_SERVER_EVENT, _sock)) {
                WARN("EventDispatch Subscribe fail.");
                return false;
            }
            String thread_name = "SSE_" + std::to_string(_sock.id); 
            SetCurrentThreadName(thread_name.c_str());
        }
        char* buff = NULL;
        size_t sz = 0;
        // 接收订单事件
        int rv = nng_recv(_sock, &buff, &sz, NNG_FLAG_ALLOC);
        if (rv != 0) {
            nng_free(buff, sz);
            return true;
        }
        // 发送给客户端
        auto status = sink.write(buff, sz);
        nng_free(buff, sz);
        return status;
    }

    void Release() {
        nng_close(_sock);
    }
private:
    std::shared_ptr<httplib::DataSink> _sinks;
    nng_socket _sock;
};

ServerEventHandler::ServerEventHandler(Server* server):HttpHandler(server) {
}

ServerEventHandler::~ServerEventHandler() {
    while (!_eventDispatchers.empty()) {
        sleep(1);
    }
}

void ServerEventHandler::get(const httplib::Request& req, httplib::Response& res) {
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*"); // 允许跨域

    res.set_chunked_content_provider("text/event-stream",
        [&](size_t offset, httplib::DataSink& sink) {
        auto id = std::this_thread::get_id();
        auto itr = _eventDispatchers.find(id);
        EventDispatcher* dispatcher = nullptr;
        if (itr == _eventDispatchers.end()) {
            // 当客户端首次连接时，将其添加到分发器
            dispatcher = new EventDispatcher();
            _eventDispatchers[id] = dispatcher;
        } else {
            dispatcher = itr->second;
        }
        
        if (!sink.is_writable() || Server::IsExit()) {
            dispatcher->Release();
            delete dispatcher;
            _eventDispatchers.erase(id);
            return false;
        }
        // 发送消息
        return dispatcher->dispatchEvent(sink);
    });
}
