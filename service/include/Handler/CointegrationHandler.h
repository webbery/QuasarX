#pragma once
#include "HttpHandler.h"
#include "Util/finance.h"

class CointegrationHandler : public HttpHandler {
public:
    CointegrationHandler(Server* server) : HttpHandler(server) {}

    virtual void get(const httplib::Request& req, httplib::Response& res);
};
