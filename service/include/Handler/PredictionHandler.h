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

class PredictionHandler: public HttpHandler {
public:
    PredictionHandler(Server* server): HttpHandler(server) {}
    void put(const httplib::Request& req, httplib::Response& res);
    void get(const httplib::Request& req, httplib::Response& res);
    void del(const httplib::Request& req, httplib::Response& res);
};
