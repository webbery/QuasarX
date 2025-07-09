#include "TraderSubsystem.h"
#include "BrokerSubSystem.h"
#include "server.h"
#include "Transfer.h"
#include "Util/log.h"

TraderSystem::TraderSystem(Server* handle, const String& dbpath): _server(handle) {
    _virtualSystem = new BrokerSubSystem(handle, nullptr);
    auto virt_path = dbpath + "/virtual.db";
    _virtualSystem->Init(virt_path.c_str(), 1000000);
}

TraderSystem::~TraderSystem() {
    if (_virtualSystem) {
        delete _virtualSystem;
    }
}
void TraderSystem::SetupSimulation(const String& name) {
    _simulations.push_back(name);
}

void TraderSystem::Start() {
    if (!_simulations.empty()) {
        _simulater_trans = new Transfer([] (nng_socket , nng_socket) {
            // TODO: read prediction and check operator
            
            return true;
        });

        _simulater_trans->start("SimulationTrader", URI_RAW_QUOTE, URI_SIM_TRADE);
    }
}


