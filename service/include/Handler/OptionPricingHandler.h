#pragma once
#include "HttpHandler.h"

/**
 * 期权定价 Handler
 *
 * POST /v0/option/pricing       — 单合约定价 (BSM / MC / Binomial)
 * POST /v0/option/pricing/multi — 多合约批量定价（对比用）
 * GET  /v0/option/iv_surface    — IV 曲面数据
 */
class OptionPricingHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void get(const httplib::Request& req, httplib::Response& res) override;
};
