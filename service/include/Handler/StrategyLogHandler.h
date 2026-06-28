#pragma once
#include "HttpHandler.h"
#include <httplib.h>

/**
 * 策略日志 HTTP Handler
 * 
 * 统一接口：GET /v0/strategy/logs
 * 
 * Query 参数：
 * - type: 查询类型（必填）
 *   - "default": 按策略/级别/时间查询（默认）
 *   - "by_symbol": 按symbol查询关联的策略日志
 *   - "stats": 获取统计信息
 *   - "strategies": 获取所有有日志的策略列表
 * - strategy: 策略名称（type=default时使用）
 * - level: 日志级别过滤（INFO/WARN/ERROR）
 * - symbol: 标的代码（type=by_symbol时使用）
 * - start_time: 开始时间（YYYY-MM-DD HH:MM:SS）
 * - end_time: 结束时间（YYYY-MM-DD HH:MM:SS）
 * - limit: 返回条数限制（默认1000）
 * - offset: 分页偏移（默认0）
 */
class StrategyLogHandler: public HttpHandler {
public:
    StrategyLogHandler(Server* server) : HttpHandler(server) {}

    void get(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;

private:
    // 按策略/级别/时间查询
    void query_default(const httplib::Request& req, httplib::Response& res);

    // 按symbol查询
    void query_by_symbol(const httplib::Request& req, httplib::Response& res);

    // 获取统计信息
    void query_stats(const httplib::Request& req, httplib::Response& res);

    // 获取策略列表
    void query_strategies(const httplib::Request& req, httplib::Response& res);

    // 辅助函数
    std::string get_param(const httplib::Request& req, const std::string& name);
    int get_int_param(const httplib::Request& req, const std::string& name, int default_val);
};
