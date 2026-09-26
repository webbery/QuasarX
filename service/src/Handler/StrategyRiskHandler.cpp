#include "Handler/StrategyRiskHandler.h"
#include "server.h"
#include "StrategySubSystem.h"
#include "Util/DecisionDB.h"
#include "Metric/Drawdown.h"
#include "Metric/Sharp.h"
#include "Metric/Return.h"
#include "Metric/Volatility.h"
#include "Metric/RiskMetric.h"
#include "Metric/CUSUMDetector.h"
#include "Util/log.h"
#include <map>
#include <algorithm>

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
    auto strategyNames = strategySystem->GetStrategyNames();

    for (const auto& name : strategyNames) {
        nlohmann::json item;
        item["id"] = name;
        item["name"] = name;
        item["type"] = GetStrategyType(name);

        // 从 DecisionDB 实盘持仓快照计算全部风险指标（单次查询）
        auto records = DecisionDB::instance().queryDailyPositions(name);
        auto metrics = ComputeRiskMetrics(records);

        item["var_95"] = metrics.var_95;
        item["max_drawdown"] = metrics.max_drawdown;
        item["sharpe_ratio"] = metrics.sharpe_ratio;
        item["win_rate"] = metrics.win_rate;
        item["var_convexity"] = metrics.var_convexity;
        item["cusum_drift_ratio"] = metrics.cusum_drift_ratio;
        item["excess_kurtosis"] = metrics.excess_kurtosis;
        item["avg_win_loss_ratio"] = metrics.avg_win_loss_ratio;
        item["cusum_signal"] = metrics.cusum_signal;
        item["cusum_triggered"] = metrics.cusum_triggered;
        item["information_ratio"] = 0.0;  // TODO: 需要基准收益序列

        response.push_back(item);
    }

    res.status = 200;
    res.set_content(response.dump(2), "application/json");
}

StrategyRiskHandler::RiskMetrics StrategyRiskHandler::ComputeRiskMetrics(
    const std::vector<DailyPositionRecord>& records) {

    RiskMetrics m;

    if (records.empty()) return m;

    // 按日期聚合: portfolio_value = Σ(position × close_price)
    std::map<time_t, double> dailyValue;
    for (const auto& rec : records) {
        dailyValue[rec.date] += rec.position * rec.close_price;
    }

    if (dailyValue.size() < 2) return m;

    // 构建有序组合价值序列
    Vector<double> portfolioValues;
    portfolioValues.reserve(dailyValue.size());
    for (const auto& [date, value] : dailyValue) {
        portfolioValues.push_back(value);
    }

    // 计算日收益率
    auto dailyReturnsVec = simple_daily_return(portfolioValues);
    std::vector<double> returns(dailyReturnsVec.begin(), dailyReturnsVec.end());

    if (returns.size() < 2) return m;

    // === 基础风险指标 ===
    int count = static_cast<int>(returns.size());
    double totalRet = simple_total_return(
        std::vector<double>(portfolioValues.begin(), portfolioValues.end()),
        portfolioValues.front());
    float annualReturn = compute_annualized_return(totalRet, count);
    double annualVol = compute_annualized_volatility(returns);
    float maxDd = max_drawdown_ratio(portfolioValues);

    m.var_95 = compute_var(returns, 0.95);
    m.max_drawdown = static_cast<double>(maxDd);
    m.sharpe_ratio = compute_sharp_ratio(annualReturn, static_cast<float>(annualVol), 0.0);
    m.win_rate = win_rate(dailyReturnsVec);

    // === CUSUM 检测 + 凸性 VaR ===
    CUSUMConfig cusumConfig;
    cusumConfig._lambda = 0.5;
    cusumConfig._threshold_multiplier = 4.0;
    cusumConfig._min_obs = 5;
    CUSUMDetector detector(cusumConfig);
    auto cusumResult = detector.detect_batch(returns);

    auto cv = compute_convexity_var(returns, detector);
    m.var_convexity = cv.var_convexity;
    m.cusum_drift_ratio = cv.drift_ratio;
    m.excess_kurtosis = cv.kurtosis;

    // === CUSUM 信号 ===
    if (cusumResult._total_change_points > 0 && !cusumResult._steps.empty()) {
        const auto& lastStep = cusumResult._steps.back();
        // _steps.size() < 10 时 size_t 减法下溢，条件恒 false → 只要有变点就触发（正确行为）
        if (lastStep._change_point ||
            cusumResult._last_change_index > cusumResult._steps.size() - 10) {
            m.cusum_triggered = true;
            m.cusum_signal = (lastStep._current_drift > 0) ? 1 : -1;
        }
    }

    // === 平均盈亏比 ===
    double sumWin = 0, sumLoss = 0;
    int nWin = 0, nLoss = 0;
    for (double r : returns) {
        if (r > 0) { sumWin += r; ++nWin; }
        else if (r < 0) { sumLoss += std::abs(r); ++nLoss; }
    }
    double avgWin = nWin > 0 ? sumWin / nWin : 0.0;
    double avgLoss = nLoss > 0 ? sumLoss / nLoss : 0.0;
    m.avg_win_loss_ratio = avgLoss > 0 ? avgWin / avgLoss : 0.0;

    return m;
}

std::string StrategyRiskHandler::GetStrategyType(const std::string& strategyName) {
    auto* strategySystem = _server->GetStrategySystem();
    if (!strategySystem) return "mixed";

    auto pools = strategySystem->GetPools(strategyName);
    if (pools.empty()) return "mixed";

    int stockCount = 0, etfCount = 0, futureCount = 0, optionCount = 0;

    for (const auto& symbol : pools) {
        switch (symbol._type) {
            case contract_type::stock:    stockCount++; break;
            case contract_type::exchange_traded_fund: etfCount++; break;
            case contract_type::future:   futureCount++; break;
            case contract_type::option:   optionCount++; break;
            default: break;
        }
    }

    int total = stockCount + etfCount + futureCount + optionCount;
    if (total == 0) return "mixed";
    if (stockCount == total) return "stock";
    if (etfCount == total) return "etf";
    if (futureCount == total) return "future";
    if (optionCount == total) return "option";
    return "mixed";
}
