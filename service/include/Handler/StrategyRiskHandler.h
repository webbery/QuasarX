#pragma once
#include "HttpHandler.h"
#include "Util/DecisionDB.h"
#include <vector>

class Server;

/**
 * 策略风险健康度 API
 * GET /v0/risk/strategies - 获取所有策略的风险指标和健康度评估数据
 *
 * 数据来源：DecisionDB daily_positions 表（实盘持仓快照）
 * 每日策略执行后 recordDailyPositions() 写入持仓+收盘价，
 * 本 handler 从该数据重建组合价值序列并计算风险指标。
 *
 * 返回字段：
 *   - id, name: 策略标识
 *   - type: 标的类型（stock/etf/option/future/mixed）
 *   - var_95: 95% VaR
 *   - max_drawdown: 最大回撤
 *   - sharpe_ratio: 夏普比率
 *   - win_rate: 胜率
 *   - var_convexity: 凸性调整后 VaR
 *   - cusum_drift_ratio: CUSUM 归一化漂移
 *   - cusum_signal: CUSUM 信号（-1/0/1）
 *   - cusum_triggered: 是否刚触发变点
 *   - excess_kurtosis: 超额峰度
 *   - avg_win_loss_ratio: 平均盈亏比
 *   - information_ratio: 信息比率（暂未实现，返回 0）
 */
class StrategyRiskHandler : public HttpHandler {
public:
    StrategyRiskHandler(Server* server);
    ~StrategyRiskHandler() = default;

    virtual void get(const httplib::Request& req, httplib::Response& res) override;

private:
    // 从实盘持仓快照计算的风险指标集合
    struct RiskMetrics {
        double var_95 = 0.0;
        double max_drawdown = 0.0;
        double sharpe_ratio = 0.0;
        double win_rate = 0.0;
        double var_convexity = 0.0;
        double cusum_drift_ratio = 0.0;
        double excess_kurtosis = 0.0;
        double avg_win_loss_ratio = 0.0;
        int cusum_signal = 0;          // -1/0/1
        bool cusum_triggered = false;
    };

    // 从 DecisionDB 持仓记录一次性计算全部风险指标（含 CUSUM 信号）
    static RiskMetrics ComputeRiskMetrics(const std::vector<DailyPositionRecord>& records);

    // 从策略的 Input 节点解析标的类型
    std::string GetStrategyType(const std::string& strategyName);
};
