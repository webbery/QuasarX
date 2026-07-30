#pragma once
#ifdef _DEBUG
#include "HttpHandler.h"

class Server;

/**
 * 模拟 Bar 注入 Handler（仅 Debug 构建生效）
 *
 * POST /v0/strategy/simulate/bar
 * Body: {
 *   "symbol": "sz.000423",
 *   "open": 25.3, "high": 25.8, "low": 25.1, "close": 25.6,
 *   "volume": 123456,
 *   "datetime": "2026-07-30 00:00:00"   // 可选，默认当天
 * }
 *
 * 流程：写入 QuoteDB → 后复权 → MarkSymbolReady → 触发策略管道
 */
class SimulateBarHandler : public HttpHandler {
public:
    SimulateBarHandler(Server* server);
    ~SimulateBarHandler() override;

    void post(const httplib::Request& req, httplib::Response& res) override;
};
#endif