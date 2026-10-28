#pragma once
#ifdef _DEBUG
#include <mutex>
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

private:
    // 仿真日推进状态：多标的可能并发推送，读写统一加锁
    std::mutex  _pushMtx;
    time_t      _lastSimDate = 0;  // 上次仿真日期，变化时触发 ResetDaily
    // 本仿真日已推送过的标的。同一标的同一日期再次推送 = 该交易日数据被改写，
    // 需要重置日终状态重跑。仅按日期判断会漏掉这种情形（同一进程内重跑用例、
    // 补数、修数据），表现为"bar 推上去了但没有任何决策"。
    Set<String> _pushedSymbols;
};
#endif
