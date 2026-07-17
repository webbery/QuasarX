#pragma once
#include "HttpHandler.h"

/// GET  /v0/risk/status?strategy=xxx — 获取风控状态（断路器级别、VaR、回撤）
/// POST /v0/risk/reset-breaker?strategy=xxx — 人工解除 Level 3 熔断
class RiskStatusHandler : public HttpHandler {
public:
    RiskStatusHandler(Server* server) : HttpHandler(server) {}
    virtual void get(const httplib::Request& req, httplib::Response& res);
    virtual void post(const httplib::Request& req, httplib::Response& res);
};
