#pragma once
#include "HttpHandler.h"

/**
 * 符号回归通用端点
 * POST /v0/sr/run — 执行符号回归搜索（SSE 流式进度 + 结果）
 *
 * 请求体通过 "task" 字段区分应用场景：
 *   - "volatility"  : 前瞻波动率公式发现
 *   - "var"         : VaR 公式搜索（规划中）
 *   - "drawdown"    : 回撤预测公式（规划中）
 *   - "slippage"    : 滑点模型发现（规划中）
 *   - "custom"      : 用户自定义目标
 */
class SRHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
};
