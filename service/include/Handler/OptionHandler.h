#pragma once
#include "HttpHandler.h"

class Server;
class OptionHandler: public HttpHandler {
public:
    OptionHandler(Server* server): HttpHandler(server) {}

    virtual void get(const httplib::Request& req, httplib::Response& res);
};

class OptionDetailHandler: public HttpHandler {
public:
    OptionDetailHandler(Server* server): HttpHandler(server) {}

    virtual void get(const httplib::Request& req, httplib::Response& res);
};
