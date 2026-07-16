#pragma once
#include "HttpHandler.h"

class Server;

/// POST /v0/capacity — 策略容量扫描
///
/// 请求:
///   strategy: 策略名（scripts/ 目录下的文件名）
///   capital_range: { min, max, steps }
///   impact_model: { eta, adv_window }
///   constraints: { max_participation_rate }
///
/// 流程:
///   1. 加载策略 JSON → 运行基准回测（零冲击）
///   2. 从 BrokerSubSystem::GetHistoryTrades 提取交易日志
///   3. Capacity::scan() 快速重放 → 输出衰减曲线
class CapacityHandler : public HttpHandler {
public:
    CapacityHandler(Server* server) : HttpHandler(server) {}
    virtual void post(const httplib::Request& req, httplib::Response& res);
};
