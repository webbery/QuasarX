#include "Handler/CapitalRiskHandler.h"
#include "server.h"
#include "json.hpp"
#include "Util/system.h"

CapitalRiskHandler::CapitalRiskHandler(Server* server)
    : HttpHandler(server), _riskManager(nullptr) {
}

CapitalRiskHandler::~CapitalRiskHandler() {
}

void CapitalRiskHandler::get(const httplib::Request& req, httplib::Response& res) {
    if (!_riskManager) {
        res.status = 500;
        res.set_content(R"({"error": "Capital risk manager not initialized"})", "application/json");
        return;
    }

    auto config = _riskManager->GetConfig();
    nlohmann::json response = _riskManager->ToJson();

    // 添加止损线数值
    response["totalStopLossLevel"] = _riskManager->GetTotalStopLossLevel();
    response["dailyLossLimitLevel"] = _riskManager->GetDailyLossLimitLevel();

    res.status = 200;
    res.set_content(response.dump(2), "application/json");
}

void CapitalRiskHandler::post(const httplib::Request& req, httplib::Response& res) {
    if (!_riskManager) {
        res.status = 500;
        res.set_content(R"({"error": "Capital risk manager not initialized"})", "application/json");
        return;
    }

    try {
        nlohmann::json params = nlohmann::json::parse(req.body);
        auto config = _riskManager->GetConfig();

        // 解析配置参数
        if (params.contains("totalStopLossPercent")) {
            double percent = params["totalStopLossPercent"];
            if (percent <= 0 || percent > 1) {
                res.status = 400;
                res.set_content(R"({"error": "totalStopLossPercent must be between 0 and 1"})", "application/json");
                return;
            }
            config._totalStopLossPercent = percent;
        }

        if (params.contains("enableTotalStopLoss")) {
            config._enableTotalStopLoss = params["enableTotalStopLoss"];
        }

        if (params.contains("autoClosePosition")) {
            config._autoClosePosition = params["autoClosePosition"];
        }

        if (params.contains("manualInterventionTimeoutSec")) {
            config._manualInterventionTimeoutSec = params["manualInterventionTimeoutSec"];
        }

        if (params.contains("timeoutAutoClose")) {
            config._timeoutAutoClose = params["timeoutAutoClose"];
        }

        if (params.contains("initialCapital")) {
            double capital = params["initialCapital"];
            if (capital <= 0) {
                res.status = 400;
                res.set_content(R"({"error": "initialCapital must be positive"})", "application/json");
                return;
            }
            _riskManager->SetInitialCapital(capital);
        }

        _riskManager->SetConfig(config);

        // 返回当前配置
        nlohmann::json response = _riskManager->ToJson();
        response["message"] = "Configuration updated successfully";

        res.status = 200;
        res.set_content(response.dump(2), "application/json");

        INFO("Capital risk config updated: totalStopLossPercent={}, enable={}",
             config._totalStopLossPercent, config._enableTotalStopLoss);

    } catch (const std::exception& e) {
        res.status = 400;
        nlohmann::json error;
        error["error"] = e.what();
        res.set_content(error.dump(), "application/json");
    }
}

DailyLossRiskHandler::DailyLossRiskHandler(Server* server)
    : HttpHandler(server), _riskManager(nullptr) {
}

DailyLossRiskHandler::~DailyLossRiskHandler() {
}

void DailyLossRiskHandler::get(const httplib::Request& req, httplib::Response& res) {
    if (!_riskManager) {
        res.status = 500;
        res.set_content(R"({"error": "Capital risk manager not initialized"})", "application/json");
        return;
    }

    nlohmann::json response = _riskManager->ToJson();

    // 添加止损线数值
    response["dailyLossLimitLevel"] = _riskManager->GetDailyLossLimitLevel();

    res.status = 200;
    res.set_content(response.dump(2), "application/json");
}

void DailyLossRiskHandler::post(const httplib::Request& req, httplib::Response& res) {
    if (!_riskManager) {
        res.status = 500;
        res.set_content(R"({"error": "Capital risk manager not initialized"})", "application/json");
        return;
    }

    try {
        nlohmann::json params = nlohmann::json::parse(req.body);
        auto config = _riskManager->GetConfig();

        // 解析配置参数
        if (params.contains("dailyMaxLossPercent")) {
            double percent = params["dailyMaxLossPercent"];
            if (percent <= 0 || percent > 1) {
                res.status = 400;
                res.set_content(R"({"error": "dailyMaxLossPercent must be between 0 and 1"})", "application/json");
                return;
            }
            config._dailyMaxLossPercent = percent;
        }

        if (params.contains("enableDailyLossLimit")) {
            config._enableDailyLossLimit = params["enableDailyLossLimit"];
        }

        if (params.contains("autoClosePosition")) {
            config._autoClosePosition = params["autoClosePosition"];
        }

        if (params.contains("manualInterventionTimeoutSec")) {
            config._manualInterventionTimeoutSec = params["manualInterventionTimeoutSec"];
        }

        if (params.contains("timeoutAutoClose")) {
            config._timeoutAutoClose = params["timeoutAutoClose"];
        }

        // 设置昨日权益（可选，用于重置）
        if (params.contains("lastDayEquity")) {
            double equity = params["lastDayEquity"];
            if (equity <= 0) {
                res.status = 400;
                res.set_content(R"({"error": "lastDayEquity must be positive"})", "application/json");
                return;
            }
            _riskManager->SetLastDayEquity(equity);
        }

        _riskManager->SetConfig(config);

        // 返回当前配置
        nlohmann::json response = _riskManager->ToJson();
        response["message"] = "Daily loss limit configuration updated successfully";

        res.status = 200;
        res.set_content(response.dump(2), "application/json");

        INFO("Daily loss risk config updated: dailyMaxLossPercent={}, enable={}",
             config._dailyMaxLossPercent, config._enableDailyLossLimit);

    } catch (const std::exception& e) {
        res.status = 400;
        nlohmann::json error;
        error["error"] = e.what();
        res.set_content(error.dump(), "application/json");
    }
}

CloseAllPositionHandler::CloseAllPositionHandler(Server* server)
    : HttpHandler(server), _riskManager(nullptr) {
}

CloseAllPositionHandler::~CloseAllPositionHandler() {
}

void CloseAllPositionHandler::post(const httplib::Request& req, httplib::Response& res) {
    if (!_riskManager) {
        res.status = 500;
        res.set_content(R"({"error": "Capital risk manager not initialized"})", "application/json");
        return;
    }

    try {
        nlohmann::json params = nlohmann::json::parse(req.body);

        // 可选：确认字段
        bool confirm = false;
        if (params.contains("confirm")) {
            confirm = params["confirm"];
        }

        if (!confirm) {
            res.status = 400;
            nlohmann::json error;
            error["error"] = "Confirmation required. Set 'confirm': true to proceed.";
            res.set_content(error.dump(), "application/json");
            return;
        }

        // 执行平仓
        _riskManager->CloseAllPositions();

        nlohmann::json response;
        response["message"] = "All positions closed successfully";
        response["equity"] = _riskManager->GetCurrentEquity();

        res.status = 200;
        res.set_content(response.dump(2), "application/json");

        WARN("Manual close all positions executed");

    } catch (const std::exception& e) {
        res.status = 500;
        nlohmann::json error;
        error["error"] = e.what();
        res.set_content(error.dump(), "application/json");
    }
}
