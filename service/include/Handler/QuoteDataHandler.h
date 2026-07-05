#pragma once
#include "HttpHandler.h"
#include <vector>
#include <string>

// 导入请求结构
struct QuoteDataRequest {
    std::string action;           // "import" / "export" / "cleanup"
    std::string table;            // 表名：stock_1d, etf_5m 等
    std::string symbol;           // 标的代码：sh.600000
    std::string adj_type;         // 复权类型：hfq / none
    std::string start_time;       // 导出起始时间
    std::string end_time;         // 导出结束时间
    std::string format;           // 导出格式：csv / json
    std::vector<std::string> csv_lines;  // CSV 行数据（仅 import 用）
};

class QuoteDataHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;

    void post(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;

private:
    // 导入 CSV 数据到 QuoteDB
    bool importCsv(const QuoteDataRequest& req, int& imported_rows, std::string& error_msg);

    // 从 QuoteDB 导出数据
    bool exportCsv(const QuoteDataRequest& req, std::string& output, std::string& error_msg);

    // 清理指定标的或表
    bool cleanup(const QuoteDataRequest& req, std::string& message, std::string& error_msg);

    // 解析 JSON 请求体
    bool parseRequest(const std::string& json_str, QuoteDataRequest& req, std::string& error_msg);
};
