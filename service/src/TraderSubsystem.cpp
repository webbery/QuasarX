#include "TraderSubsystem.h"
#include "BrokerSubSystem.h"
#include "server.h"
#include "Transfer.h"
#include "Util/log.h"
#include "DataSource.h"

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
        _simulater_trans = new Transfer([this] (nng_socket from, nng_socket to) {
            DataFeatures dm;
            if (!ReadFeatures(from, dm)) {
                return false;
            }
            // read prediction and check operator
            for (auto symb: dm._symbols) {
                fixed_time_range tr;
                int op = 0;
                if (_virtualSystem->GetNextPrediction(symb, tr, op)) {
                    auto current = Now();
                    if (tr == current) {
                        if (op & (int)ContractOperator::Buy) {
                            // _virtualSystem->Buy(, )
                            StrategyBuy(symb, dm);
                        }
                        if (op & (int)ContractOperator::Sell) {

                        }
                    }
                }
            }
            
            return false;
        });

        _simulater_trans->start("SimulationTrader", URI_FEATURE, URI_SIM_TRADE);
    }
}

void TraderSystem::StrategyBuy(symbol_t symbol, const DataFeatures& features) {
    // TODO: 分批多次入场

    float vwap = -1;
    for (int i = 0; i < features._data.size(); ++i) {
        if (features._features[i] == FeatureType::FT_VWAP) {
            vwap = features._data[i];
            break;
        }
    }

    
}
