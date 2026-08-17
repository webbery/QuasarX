#include "Handler/StrategyPerformanceHandler.h"
#include "Util/DecisionDB.h"
#include "Metric/Return.h"
#include "Metric/Drawdown.h"
#include "Metric/Sharp.h"
#include "Metric/Volatility.h"
#include "Util/string_algorithm.h"
#include "server.h"
#include <map>

void StrategyPerformanceHandler::get(const httplib::Request& req, httplib::Response& res) {
    String strategy = req.get_param_value("name");
    if (strategy.empty()) {
        res.status = 400;
        res.set_content(R"({"error": "missing 'name' parameter"})", "application/json");
        return;
    }

    // 解析可选日期范围
    time_t startDate = 0, endDate = 0;
    String startStr = req.get_param_value("start");
    String endStr = req.get_param_value("end");
    if (!startStr.empty()) startDate = FromStr(startStr, "%Y-%m-%d");
    if (!endStr.empty()) endDate = FromStr(endStr, "%Y-%m-%d");

    // 查询持仓快照
    auto records = DecisionDB::instance().queryDailyPositions(strategy, startDate, endDate);
    if (records.empty()) {
        nlohmann::json result;
        result["strategy"] = strategy;
        result["trading_days"] = 0;
        result["message"] = "no position data found";
        res.set_content(result.dump(), "application/json");
        return;
    }

    // 按日期聚合：portfolio_value = Σ(position × close_price)
    std::map<time_t, double> dailyValue;
    for (const auto& rec : records) {
        dailyValue[rec.date] += rec.position * rec.close_price;
    }

    // 构建有序序列
    Vector<double> portfolioValues;
    portfolioValues.reserve(dailyValue.size());
    for (const auto& [date, value] : dailyValue) {
        portfolioValues.push_back(value);
    }

    if (portfolioValues.size() < 2) {
        nlohmann::json result;
        result["strategy"] = strategy;
        result["trading_days"] = static_cast<int>(portfolioValues.size());
        result["message"] = "insufficient data (need >= 2 days)";
        res.set_content(result.dump(), "application/json");
        return;
    }

    // 计算指标（复用回测公式）
    double initialCapital = portfolioValues.front();
    auto dailyReturns = simple_daily_return(portfolioValues);
    double totalReturn = simple_total_return(portfolioValues, initialCapital);
    int count = static_cast<int>(dailyReturns.size());
    float annualReturn = compute_annualized_return(totalReturn, count);
    double annualVol = compute_annualized_volatility(dailyReturns);
    float sharpe = compute_sharp_ratio(annualReturn, static_cast<float>(annualVol), 0.0);
    float maxDd = max_drawdown_ratio(portfolioValues);
    float winRate = win_rate(dailyReturns);
    float calmar = calmar_ratio(annualReturn, maxDd);

    // 构建响应
    nlohmann::json result;
    result["strategy"] = strategy;
    result["trading_days"] = count;
    result["initial_capital"] = initialCapital;
    result["final_value"] = portfolioValues.back();

    nlohmann::json metrics;
    metrics["total_return"] = totalReturn;
    metrics["annual_return"] = annualReturn;
    metrics["annual_volatility"] = annualVol;
    metrics["sharpe_ratio"] = sharpe;
    metrics["max_drawdown"] = static_cast<double>(maxDd);
    metrics["win_rate"] = winRate;
    metrics["calmar_ratio"] = calmar;
    result["metrics"] = metrics;

    res.set_content(result.dump(2), "application/json");
}
