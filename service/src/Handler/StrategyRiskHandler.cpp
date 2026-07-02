#include "Handler/StrategyRiskHandler.h"
#include "BrokerSubSystem.h"
#include "server.h"
#include "StrategySubSystem.h"
#include "AgentSubSystem.h"
#include "Metric/CUSUMDetector.h"
#include "Util/system.h"
#include "Util/log.h"

StrategyRiskHandler::StrategyRiskHandler(Server* server)
    : HttpHandler(server) {
}

void StrategyRiskHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto* strategySystem = _server->GetStrategySystem();
    
    if (!strategySystem) {
        res.status = 500;
        res.set_content(R"({"error": "Strategy system not initialized"})", "application/json");
        return;
    }

    nlohmann::json response = nlohmann::json::array();
    
    // 获取所有策略名称
    auto strategyNames = strategySystem->GetStrategyNames();
    
    for (const auto& name : strategyNames) {
        nlohmann::json item;
        item["id"] = name;
        item["name"] = name;
        
        // 获取策略类型（从 Input 节点解析）
        item["type"] = GetStrategyType(name);
        
        // 获取统计指标
        const auto& collections = strategySystem->GetIndicators(name);
        
        // 信息比率 (IR)
        auto irIt = collections.find(StatisticIndicator::Infomation);
        if (irIt != collections.end()) {
            item["information_ratio"] = std::get<float>(irIt->second);
        } else {
            item["information_ratio"] = 0.0;
        }
        
        // VaR (95%)
        auto varIt = collections.find(StatisticIndicator::VaR);
        if (varIt != collections.end()) {
            item["var_95"] = std::get<float>(varIt->second);
        } else {
            item["var_95"] = 0.0;
        }
        
        // 最大回撤
        auto ddIt = collections.find(StatisticIndicator::MaxDrawDown);
        if (ddIt != collections.end()) {
            item["max_drawdown"] = std::get<float>(ddIt->second);
        } else {
            item["max_drawdown"] = 0.0;
        }
        
        // 夏普比率
        auto sharpIt = collections.find(StatisticIndicator::Sharp);
        if (sharpIt != collections.end()) {
            item["sharpe_ratio"] = std::get<float>(sharpIt->second);
        } else {
            item["sharpe_ratio"] = 0.0;
        }
        
        // 胜率
        auto wrIt = collections.find(StatisticIndicator::WinRate);
        if (wrIt != collections.end()) {
            item["win_rate"] = std::get<float>(wrIt->second);
        } else {
            item["win_rate"] = 0.0;
        }
        
        // CUSUM 信号（调仓时重置方案）
        // TODO: 当前方案存在以下问题：
        //   1. 调仓后数据不足时 CUSUM 不够敏感
        //   2. 频繁调仓时 CUSUM 无法累积足够历史
        //   3. 未来可改为"加权收益率"方案，用持仓权重加权计算组合收益率
        auto cusumResult = CalculateCusumSignal(name);
        item["cusum_signal"] = cusumResult.signal;
        item["cusum_triggered"] = cusumResult.triggered;
        
        response.push_back(item);
    }
    
    res.status = 200;
    res.set_content(response.dump(2), "application/json");
}

std::string StrategyRiskHandler::GetStrategyType(const std::string& strategyName) {
    // 从策略的 Input 节点获取标的池，判断类型
    auto* strategySystem = _server->GetStrategySystem();
    if (!strategySystem) return "mixed";
    
    auto pools = strategySystem->GetPools(strategyName);
    if (pools.empty()) return "mixed";
    
    // 统计各类标的数量
    int stockCount = 0, etfCount = 0, futureCount = 0, optionCount = 0;
    
    for (const auto& symbol : pools) {
        switch (symbol._type) {
            case contract_type::stock:
                stockCount++;
                break;
            case contract_type::exchange_traded_fund:
                etfCount++;
                break;
            case contract_type::future:
                futureCount++;
                break;
            case contract_type::put:
            case contract_type::call:
                optionCount++;
                break;
            default:
                break;
        }
    }
    
    // 判断主要类型
    int total = stockCount + etfCount + futureCount + optionCount;
    if (total == 0) return "mixed";
    
    // 如果只有一种类型
    if (stockCount == total) return "stock";
    if (etfCount == total) return "etf";
    if (futureCount == total) return "future";
    if (optionCount == total) return "option";
    
    // 混合类型
    return "mixed";
}

StrategyRiskHandler::CusumResult StrategyRiskHandler::CalculateCusumSignal(const std::string& strategyName) {
    CusumResult result;
    
    auto* strategySystem = _server->GetStrategySystem();
    if (!strategySystem) return result;
    
    // 通过 StrategySubSystem 获取 FlowSubsystem（AgentSubSystem）
    auto* agentSystem = strategySystem->GetFlowSubsystem();
    if (!agentSystem) return result;
    
    // 获取策略的日收益率数据
    auto dailyReturns = agentSystem->GetBacktestDailyReturns(strategyName);
    
    if (dailyReturns.returns.empty() || dailyReturns.returns.size() < 5) {
        // 数据不足，返回中性信号
        return result;
    }
    
    // TODO: 当前实现使用全部日收益率运行 CUSUM
    // 问题：调仓操作会改变策略的"性格"，但 CUSUM 没有感知
    // 理想方案：检测调仓时间点，在调仓后重置 CUSUM 状态
    // 或者：使用持仓权重加权计算组合收益率
    
    // 将 Vector<double> 转换为 std::vector<double>
    std::vector<double> returnsVec(dailyReturns.returns.begin(), dailyReturns.returns.end());
    
    // 使用默认配置创建 CUSUMDetector
    CUSUMConfig config;
    config._lambda = 0.5;              // 容许偏差倍数
    config._threshold_multiplier = 4.0; // 阈值倍数
    config._min_obs = 5;               // 最少观测数（小样本）
    
    CUSUMDetector detector(config);
    
    // 批量检测
    auto cusumResult = detector.detect_batch(returnsVec);
    
    // 解释信号
    // TODO: 当前使用 ChangePoint 模式（触发=1，否则=0）
    // 未来可根据策略类型选择不同模式：
    //   - Momentum: S+触发=+1, S-触发=-1
    //   - MeanRevert: S+触发=-1, S-触发=+1
    
    // 检查是否有变点触发
    if (cusumResult._total_change_points > 0 && !cusumResult._steps.empty()) {
        // 获取最后一步的结果
        const auto& lastStep = cusumResult._steps.back();
        
        // 如果最后一步触发了变点，或者最近有变点发生
        if (lastStep._change_point || cusumResult._last_change_index > cusumResult._steps.size() - 10) {
            result.triggered = true;
            // 根据漂移方向判断信号
            if (lastStep._current_drift > 0) {
                result.signal = 1;   // 正向漂移，策略表现改善
            } else {
                result.signal = -1;  // 负向漂移，策略表现恶化
            }
        }
    }
    
    return result;
}
