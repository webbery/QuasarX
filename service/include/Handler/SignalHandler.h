#pragma once
#include "HttpHandler.h"

class SignalHandler: public HttpHandler {
public:
    SignalHandler(Server* server): HttpHandler(server) {}
    virtual void get(const httplib::Request& req, httplib::Response& res);
};
