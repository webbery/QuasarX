#include "TraderSubsystem.h"
#include "BrokerSubSystem.h"
#include "server.h"
#include "Transfer.h"
#include "DataSource.h"
#include "Bridge/CTP/CTPSymbol.h"
#include "Strategy.h"
#include <limits>
#include <string>
#include "Features/VWAP.h"

TraderSystem::TraderSystem(Server* handle, const String& dbpath): _server(handle) {
    _virtualSystem = handle->GetVirtualSubSystem();
}

TraderSystem::~TraderSystem() {
}
void TraderSystem::SetupSimulation(const String& name) {
    _simulations.push_back(name);
}

void TraderSystem::Start() {
    _stock_working_range = GetWorkingRange(ExchangeName::MT_Beijing);

    if (!_simulations.empty()) {
        _simulater_trans = new Transfer([this] (nng_socket from, nng_socket to) {
            DataFeatures dm;
            if (!ReadFeatures(from, dm)) {
                return true;
            }
            // read prediction and check operator
            auto symb = dm._symbol;
            fixed_time_range tr;
            int op = 0;
            if (!_virtualSystem->GetNextPrediction(symb, tr, op))
                return true;

            auto current = Now();
            if (tr == current) {
                // thread_local char 
                if (op & (int)ContractOperator::Long) {
                    DEBUG_INFO("Buy");
                    if (StrategyBuy(symb, dm)) {
                        _virtualSystem->DoneForecast(symb, op);
                    }
                }
                if (op & (int)ContractOperator::Sell) {
                    DEBUG_INFO("Sell");
                    if (StrategySell(symb, dm)) {
                        _virtualSystem->DoneForecast(symb, op);
                    }
                }
                if (op & (int)ContractOperator::Short) {
                    DEBUG_INFO("Short");
                }
            }
            
            return true;
        });

        _simulater_trans->start("SimulationTrader", URI_FEATURE, URI_SIM_TRADE);
    }
}

bool TraderSystem::StrategyBuy(symbol_t symbol, const DataFeatures& features) {
    // TODO: 分批多次入场

    float vwap = -1;
    for (int i = 0; i < features._data.size(); ++i) {
        if (features._features[i] == std::hash<StringView>()(VWAPFeature::name())) {
            vwap = features._data[i];
            break;
        }
    }

    if (features._price < vwap || IsNearClose(symbol)) {
        Order order;
        order._type = OrderType::Market;
        order._number = 0;
        order._order[0]._price = features._price;
        DealDetail dd;
        if (_virtualSystem->Buy(symbol, order, dd) == OrderStatus::All) {
            _server->SendEmail("Buy " + get_symbol(symbol) + "[price: " + std::to_string(features._price) + "]");
            return true;
        }
    }
    return false;
}

bool TraderSystem::StrategySell(symbol_t symbol, const DataFeatures& features) {
    float vwap = std::numeric_limits<float>::max();
    for (int i = 0; i < features._data.size(); ++i) {
        if (features._features[i] == std::hash<StringView>()(VWAPFeature::name())) {
            vwap = features._data[i];
            break;
        }
    }
    if (features._price > vwap || IsNearClose(symbol)) {
        Order order;
        order._number = 0;
        order._order[0]._price = features._price;
        DealDetail dd;
        if (_virtualSystem->Sell(symbol, order, dd) == OrderStatus::All) {
            LOG("sell order: {}, result: {}", order, dd);
            _server->SendEmail("Sell " + get_symbol(symbol) + "[price: " + std::to_string(features._price) + "]");
            return true;
        }
    }
    return false;
}

bool TraderSystem::StrategyShort(symbol_t symbol, const DataFeatures& features) {
    // TODO: 借入

    // 卖出
    return StrategySell(symbol, features);
}

bool TraderSystem::IsNearClose(symbol_t symb) {
    auto current = Now();
    auto name = GetExchangeName(get_symbol(symb));
    auto final_time = *_stock_working_range.rbegin();
    if (current == final_time && final_time.near_end(current)) {
        INFO("force buy because near trade end.");
        return true;
    }
    return false;
}
