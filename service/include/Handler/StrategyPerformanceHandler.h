#pragma once
#include "HttpHandler.h"

class StrategyPerformanceHandler : public HttpHandler {
public:
    StrategyPerformanceHandler(Server* server) : HttpHandler(server) {}
    virtual void get(const httplib::Request& req, httplib::Response& res) override;
};
