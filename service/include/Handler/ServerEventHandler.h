#pragma once
#include "HttpHandler.h"

class EventDispatcher;

class ServerEventHandler: public HttpHandler {
public:
    ServerEventHandler(Server* server);
    ~ServerEventHandler();

    virtual void get(const httplib::Request& req, httplib::Response& res);
private:
    EventDispatcher* _eventDispatcher;
};
