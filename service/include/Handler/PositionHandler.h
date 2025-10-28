#pragma once

#include "HttpHandler.h"
class PositionHandler: public HttpHandler {
public:
    PositionHandler(Server*);

    virtual void get(const httplib::Request& req, httplib::Response& res);
private:
};