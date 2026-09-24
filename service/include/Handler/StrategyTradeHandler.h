#pragma once
#include "HttpHandler.h"

class StrategyTradeHandler : public HttpHandler {
public:
    StrategyTradeHandler(Server* server) : HttpHandler(server) {}
    virtual void get(const httplib::Request& req, httplib::Response& res) override;
};
