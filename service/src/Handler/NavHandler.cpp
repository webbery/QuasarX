#include "Handler/NavHandler.h"
#include "BrokerSubSystem.h"
#include "server.h"
#include "json.hpp"

void NavHandler::get(const httplib::Request& req, httplib::Response& res) {
    String strategyStr = req.get_param_value("strategy");
    String startStr = req.get_param_value("start");
    String endStr = req.get_param_value("end");

    if (strategyStr.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", "strategy parameter is required"}}.dump(), "application/json");
        return;
    }

    uint32_t strategyHash = 0;
    try {
        strategyHash = std::stoul(strategyStr);
    } catch (...) {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", "invalid strategy parameter"}}.dump(), "application/json");
        return;
    }

    time_t start = startStr.empty() ? 0 : std::stoll(startStr);
    time_t end = endStr.empty() ? std::numeric_limits<time_t>::max() : std::stoll(endStr);

    auto* broker = _server->GetBrokerSubSystem();
    if (!broker) {
        res.status = 500;
        res.set_content(nlohmann::json{{"error", "broker system not available"}}.dump(), "application/json");
        return;
    }

    auto nav = broker->QueryNav(strategyHash, start, end);

    nlohmann::json result;
    result["dates"] = nlohmann::json::array();
    result["values"] = nlohmann::json::array();
    for (size_t i = 0; i < nav.dates.size(); ++i) {
        result["dates"].push_back((int64_t)nav.dates[i]);
        result["values"].push_back(nav.values[i]);
    }

    res.set_content(result.dump(), "application/json");
}
