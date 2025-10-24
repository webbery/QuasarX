#pragma once
#include "HttpHandler.h"
#include <thread>

class EventDispatcher;

class ServerEventHandler: public HttpHandler {
public:
    ServerEventHandler(Server* server);
    ~ServerEventHandler();

    virtual void get(const httplib::Request& req, httplib::Response& res);
private:
    Map<std::thread::id, EventDispatcher*> _eventDispatchers;
};
