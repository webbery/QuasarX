#pragma once
#include "HttpHandler.h"

class Server;
class ReplayHandler: public HttpHandler {
public:
    ReplayHandler(Server* server): HttpHandler(server) {}

    virtual void post(const httplib::Request &req, httplib::Response &res);

private:
    void HandleTicksQuery(const httplib::Request &req, httplib::Response &res);
    void HandleDeleteTickData(const httplib::Request &req, httplib::Response &res);
};
