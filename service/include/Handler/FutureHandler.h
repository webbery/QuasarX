#pragma once
#include "HttpHandler.h"

class FutureHandler: public HttpHandler {
public:
    FutureHandler(Server* server): HttpHandler(server) {}

    virtual void get(const httplib::Request& req, httplib::Response& res);
};
