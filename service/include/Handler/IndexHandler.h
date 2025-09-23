#pragma once

#include "HttpHandler.h"
#include "nng/nng.h"
class IndexHandler: public HttpHandler {
public:
    IndexHandler(Server* server);
    ~IndexHandler();

    virtual void get(const httplib::Request& req, httplib::Response& res);
private:
    int _times;

};