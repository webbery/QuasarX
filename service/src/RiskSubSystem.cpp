#include "RiskSubSystem.h"
#include "Risk/StopLoss.h"
#include "Util/system.h"
#include "server.h"

RiskSubSystem::RiskSubSystem(Server* handle):_handle(handle) {

}

RiskSubSystem::~RiskSubSystem() {
    for (auto& item: _stoploss) {
        delete item.second;
    }
    _stoploss.clear();
}

bool RiskSubSystem::Init(nlohmann::json& config) {
    for (auto& item: config) {
        // 止损设置
        String email = item["email"];
        auto& targets = item["target"];
        for (auto& target: targets) {
            String name = target["symbol"];
            auto exc = _handle->GetExchange(name);
            auto ct = _handle->GetContractType(name);
            auto s = to_symbol(name);
            double last_price = target["price"];
            StopLossInfo info;
            if (target.contains("percent")) {
                IStopLoss* sl = new SLPercentage();
                _stoploss[s] = sl;
            }
        }
    }
    return true;
}

void RiskSubSystem::UpdateRisk(IRiskMetric* risk) {

}

void RiskSubSystem::Metric() {

}

