#pragma once
#include "HttpHandler.h"

class Server;
class BackTestHandler: public HttpHandler {
public:
    BackTestHandler(Server* server);

    virtual void post(const httplib::Request& req, httplib::Response& res);

private:
    void InitSymbols();
};