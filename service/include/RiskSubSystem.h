#pragma once
#include "DataContext.h"
#include "Util/system.h"
#include "json.hpp"

class Server;
class IStopLoss;
class IRiskMetric;
class CapitalRiskManager;

class RiskSubSystem {
public:
    RiskSubSystem(Server* handle);
    ~RiskSubSystem();

    bool Init(nlohmann::json& config);

    void UpdateRisk(IRiskMetric* risk);

    /**
     * 风控检查 - 在策略图执行后调用
     */
    void Metric(const DataContext& context);

    /**
     * 获取资金风险管理器
     */
    CapitalRiskManager* GetCapitalRiskManager() { return _capitalRiskManager; }

private:

    void SendEMail();

private:
    Server* _handle;
    nng_socket _sock;

    Map<symbol_t, IStopLoss*> _stoploss;

    CapitalRiskManager* _capitalRiskManager;
};