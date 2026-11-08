#pragma once
#include "HttpHandler.h"

class FinanceHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void get(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;

private:
    /// 进程内唯一的递增任务 id：用于把 SSE 事件关联回发起它的下载任务
    String next_task_id();
};
