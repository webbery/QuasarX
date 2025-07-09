#pragma once

#include "HttpHandler.h"
class IndexHandler: public HttpHandler {
public:
    IndexHandler(Server* server);

    virtual void get(const httplib::Request& req, httplib::Response& res);
private:

};