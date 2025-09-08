#pragma once
#include "Util/system.h"
#include "json.hpp"

class Server;
class IStopLoss;
class IRiskMetric;

class RiskSubSystem {
public:
    RiskSubSystem(Server* handle);
    ~RiskSubSystem();

    bool Init(nlohmann::json& config);

    void UpdateRisk(IRiskMetric* risk);

    void Metric();

private:

    void SendEMail();
    
private:
    Server* _handle;
    nng_socket _sock;

    Map<symbol_t, IStopLoss*> _stoploss;
};