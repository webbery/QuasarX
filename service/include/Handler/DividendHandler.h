#pragma once
#include "HttpHandler.h"

/**
 * 分红除权数据 Handler
 *
 * POST /v0/dividend  — 手动触发下载（从 BaoStock 拉取并导入 DuckDB）
 * GET  /v0/dividend  — 查询（date=YYYY-MM-DD 或 code=XXXXXX.SH）
 * DEL  /v0/dividend  — 删除指定标的的分红数据
 */
class DividendHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void get(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;
};
