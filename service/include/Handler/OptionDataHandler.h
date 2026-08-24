#pragma once
#include "HttpHandler.h"

/**
 * 期权日终数据 Handler
 *
 * 数据源: CFFEX / SSE / SZSE 官方日终行情
 * 存储: OptionDataDB::option_daily 表 (DuckDB)
 *
 * POST   /v0/option/data  — 触发下载（exchange + product[] + start + end）
 *                            后台线程: python tools/fetch_<exchange>_option.py
 *                            自动 importCsv 到 DuckDB, SSE 进度推送
 * GET    /v0/option/data  — 查询
 *                            exchange= filter → listContracts(按 exchange)
 *                            product=  filter → listContracts(按 product)
 *                            contract= → queryByContract(单合约历史)
 *                            无参数     → 列出全部合约概要
 * DELETE /v0/option/data  — 删除指定合约或清空所有
 *                            contract= → 删除该合约
 *                            无参数    → 清空整张表
 */
class OptionDataHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void get(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;
};
