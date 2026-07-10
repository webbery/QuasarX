#pragma once
#include "HttpHandler.h"

struct FinanceDataRequest {
    std::string action;           // "import" / "export" / "cleanup"
    std::string category;         // 类别: profit/operation/growth/balance/cashflow/dupont
    std::string code;             // 标的代码: sh.600519
    std::string start_date;       // 导出起始日期
    std::string end_date;         // 导出结束日期
    std::string format;           // 导出格式: csv / json
    std::vector<std::string> csv_lines;  // CSV 行数据（仅 import 用）
};

class FinanceDataHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;

private:
    bool importCsv(const FinanceDataRequest& req, int& imported_rows, std::string& error_msg);
    bool exportCsv(const FinanceDataRequest& req, std::string& output, std::string& error_msg);
    bool cleanup(const FinanceDataRequest& req, std::string& message, std::string& error_msg);
    bool parseRequest(const std::string& json_str, FinanceDataRequest& req, std::string& error_msg);
};
