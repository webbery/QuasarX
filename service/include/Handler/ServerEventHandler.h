#pragma once
#include "HttpHandler.h"
#include <thread>

class ServerEventHandler: public HttpHandler {
public:
    ServerEventHandler(Server* server);
    ~ServerEventHandler();

    virtual void get(const httplib::Request& req, httplib::Response& res);

private:
    void run();
private:
    static std::thread* _dispather;
};
