#pragma once
#include "HttpHandler.h"

/**
 * 两融（融资融券）明细数据 Handler
 *
 * 数据落 FinanceDB 的 margin_daily 表（类目通道，字段口径见
 * tools/fetch_margin_data.py 与 FinanceDB::CATEGORY_DEFS["margin_daily"]）。
 *
 * POST   /v0/margin  — 下载并导入（后台线程，SSE 推 "margin_download" 进度）
 *                      body（可空）:
 *                        {"date": "20261110"}   指定交易日，缺省取最近有数据的交易日
 *                        {"env":  "py311"}      python 环境名（配置的 python.environments）
 *                      响应: {"status":"started","date":"..."}
 *
 * GET    /v0/margin  — 查询
 *                      ?symbol=sh.600000[&start=YYYY-MM-DD][&end=YYYY-MM-DD][&limit=20]
 *                      ?date=YYYY-MM-DD[&limit=10000]  某交易日全市场
 *                      无参数 / ?summary=1             数据概览（最新日期、行数、近 N 日）
 *
 * DELETE /v0/margin  — 删除
 *                      ?symbol=sh.600000[&start][&end]  删某标的一段
 *                      ?date=YYYY-MM-DD                 删某一天
 *                      ?all=1                           清空整张表（必须显式给）
 *                      响应: {"status":"deleted","rows":N}
 */
class MarginHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void get(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;

private:
    /// 懒初始化 FinanceDB；失败时已经写好 500 响应，返回 false
    bool ensureDB(httplib::Response& res);
};
