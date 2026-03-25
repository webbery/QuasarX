#include "RiskSubSystem.h"
#include "Risk/StopLoss.h"
#include "Risk/CapitalRiskManager.h"
#include "Util/system.h"
#include "server.h"
#include "DataContext.h"

RiskSubSystem::RiskSubSystem(Server* handle): _handle(handle), _capitalRiskManager(nullptr) {

}

RiskSubSystem::~RiskSubSystem() {
    for (auto& item: _stoploss) {
        delete item.second;
    }
    _stoploss.clear();
    if (_capitalRiskManager) {
        delete _capitalRiskManager;
    }
}

bool RiskSubSystem::Init(nlohmann::json& config) {
    // 初始化资金风险管理器
    _capitalRiskManager = new CapitalRiskManager(_handle);
    nlohmann::json capitalConfig = nlohmann::json::object();
    if (config.contains("capitalRisk")) {
        capitalConfig = config["capitalRisk"];
    }
    _capitalRiskManager->Init(capitalConfig);

    // 设置报警回调
    _capitalRiskManager->SetAlertCallback([this](const String& message, bool isTotalStopLoss) {
        WARN("[CapitalRisk] Alert: {}", message);
        // TODO: 发送邮件/SSE 推送
    });

    // 设置自动平仓回调
    _capitalRiskManager->SetClosePositionCallback([this]() {
        INFO("[CapitalRisk] Executing close all positions...");
        // 平仓逻辑在 CapitalRiskManager::CloseAllPositions 中实现
    });

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

void RiskSubSystem::Metric(const DataContext& context) {
    // 调用资金风控检查
    if (_capitalRiskManager) {
        // 获取当前权益并检查风控
        double currentEquity = _capitalRiskManager->GetCurrentEquity();
        _capitalRiskManager->CheckRisk(currentEquity);
    }
}

