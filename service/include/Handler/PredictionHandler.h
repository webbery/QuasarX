#pragma once
#include "HttpHandler.h"

class MonteCarloHandler : public HttpHandler {
public:
    MonteCarloHandler(Server* server): HttpHandler(server) {}
    
    void post(const httplib::Request& req, httplib::Response& res);

};

class FiniteDifferenceHandler : public HttpHandler {
public:
    FiniteDifferenceHandler(Server* server): HttpHandler(server) {}
    void post(const httplib::Request& req, httplib::Response& res);
};