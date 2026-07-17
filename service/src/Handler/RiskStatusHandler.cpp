#include "Handler/RiskStatusHandler.h"
#include "server.h"
#include "RiskSubSystem.h"
#include "Risk/CapitalRiskManager.h"
#include "json.hpp"

void RiskStatusHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto riskSys = _server->GetRiskSubSystem();
    if (!riskSys) {
        res.status = 503;
        res.set_content(R"({"message":"RiskSubSystem not available"})", "application/json");
        return;
    }

    auto* riskMgr = riskSys->GetCapitalRiskManager();
    if (!riskMgr) {
        res.status = 503;
        res.set_content(R"({"message":"CapitalRiskManager not available"})", "application/json");
        return;
    }

    nlohmann::json status = riskMgr->GetRiskStatus();
    status["strategy"] = req.get_param_value("strategy");

    res.status = 200;
    res.set_content(status.dump(), "application/json");
}

void RiskStatusHandler::post(const httplib::Request& req, httplib::Response& res) {
    // POST /v0/risk/reset-breaker — 人工解除熔断
    auto riskSys = _server->GetRiskSubSystem();
    if (!riskSys) {
        res.status = 503;
        res.set_content(R"({"message":"RiskSubSystem not available"})", "application/json");
        return;
    }

    auto* riskMgr = riskSys->GetCapitalRiskManager();
    if (!riskMgr) {
        res.status = 503;
        res.set_content(R"({"message":"CapitalRiskManager not available"})", "application/json");
        return;
    }

    riskMgr->ResetBreaker();

    nlohmann::json result;
    result["message"] = "Breaker reset successfully";
    result["breaker_level"] = riskMgr->GetBreakerLevel();

    res.status = 200;
    res.set_content(result.dump(), "application/json");
}
