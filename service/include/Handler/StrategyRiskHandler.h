#pragma once
#include "HttpHandler.h"

class Server;

/**
 * 策略风险健康度 API
 * GET /v0/risk/strategies - 获取所有策略的风险指标和健康度评估数据
 *
 * 返回字段：
 *   - id, name: 策略标识
 *   - type: 标的类型（stock/etf/option/future/mixed）
 *   - information_ratio: 信息比率
 *   - cusum_signal: CUSUM 信号（-1/0/1）
 *   - cusum_triggered: 是否刚触发变点
 *   - var_95: 95% VaR
 *   - max_drawdown: 最大回撤
 *   - sharpe_ratio: 夏普比率
 *   - win_rate: 胜率
 *
 * TODO: CUSUM 计算当前使用"调仓时重置"方案，存在以下问题：
 *   1. 调仓后数据不足时 CUSUM 不够敏感
 *   2. 频繁调仓时 CUSUM 无法累积足够历史
 *   3. 未来可改为"加权收益率"方案，用持仓权重加权计算组合收益率
 */
class StrategyRiskHandler : public HttpHandler {
public:
    StrategyRiskHandler(Server* server);
    ~StrategyRiskHandler() = default;

    virtual void get(const httplib::Request& req, httplib::Response& res) override;

private:
    // 从策略的 Input 节点解析标的类型
    std::string GetStrategyType(const std::string& strategyName);

    // 计算策略的 CUSUM 信号（调仓时重置方案）
    struct CusumResult {
        int signal = 0;           // -1/0/1
        bool triggered = false;
    };
    CusumResult CalculateCusumSignal(const std::string& strategyName);
};
