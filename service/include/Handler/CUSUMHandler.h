#pragma once
#include "HttpHandler.h"

class CUSUMHandler : public HttpHandler {
public:
    CUSUMHandler(Server* server) : HttpHandler(server) {}
    virtual void get(const httplib::Request& req, httplib::Response& res);
    virtual void post(const httplib::Request& req, httplib::Response& res);
};
