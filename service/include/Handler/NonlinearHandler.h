#pragma once
#include "HttpHandler.h"

class NonlinearHandler: public HttpHandler {
public:
    NonlinearHandler(Server* server): HttpHandler(server) {}
    virtual void get(const httplib::Request& req, httplib::Response& res);
};
