#pragma once
#include "HttpHandler.h"

class Server;
class NavHandler : public HttpHandler {
public:
    NavHandler(Server* server) : HttpHandler(server) {}

    virtual void get(const httplib::Request& req, httplib::Response& res) override;
};
