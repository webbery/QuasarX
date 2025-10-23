#include "Handler/ServerEventHandler.h"
#include "Util/system.h"
#include "nng/nng.h"
#include "server.h"

class EventDispatcher {
public:
    EventDispatcher() { _sock.id = 0; }
    ~EventDispatcher() {

    }

    void dispatchEvent(const std::string& data, const std::string& eventType = "message", const std::string& id = "") {
        if (_sock.id == 0 && !Subscribe(URI_SERVER_EVENT, _sock)) {
            WARN("EventDispatch Subscribe fail.");
            return;
        }
        SetCurrentThreadName("SSE");
        while (!Server::IsExit()) {
            char* buff = NULL;
            size_t sz = 0;
            // 接收订单事件
            int rv = nng_recv(_sock, &buff, &sz, NNG_FLAG_ALLOC);
            if (rv != 0) {
                nng_free(buff, sz);
                continue;;
            }
            // 发送给客户端
        }
        nng_close(_sock);
    }
private:
    std::shared_ptr<httplib::DataSink> _sinks;
    nng_socket _sock;
};

ServerEventHandler::ServerEventHandler(Server* server):HttpHandler(server) {
    _eventDispatcher = new EventDispatcher();
}

ServerEventHandler::~ServerEventHandler() {
    delete _eventDispatcher;
}

void ServerEventHandler::get(const httplib::Request& req, httplib::Response& res) {
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*"); // 允许跨域

    res.set_chunked_content_provider("text/event-stream",
        [&](size_t offset, httplib::DataSink& sink) {
        // 当客户端首次连接时，将其添加到分发器
        //auto sink_ptr = std::make_shared<httplib::DataSink>(sink);
        // dispatcher.addClient(sink_ptr);
        
        // 发送欢迎消息
        _eventDispatcher->dispatchEvent("连接已建立", "system", "welcome");
        
        // 这里保持连接打开，实际应用中可以通过条件变量等待新消息
        // 返回true保持连接，返回false关闭连接:cite[1]
        return false; 
    });
}
