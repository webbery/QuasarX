#include "TraderSubsystem.h"
#include "BrokerSubSystem.h"
#include "DataGroup.h"
#include "server.h"
#include "Transfer.h"
#include "DataSource.h"
#include "Bridge/CTP/CTPSymbol.h"
#include "Strategy.h"
#include <limits>
#include <mutex>
#include <string>
#include <variant>
#include "Features/VWAP.h"
#include "RiskSubSystem.h"

TraderSystem::TraderSystem(Server* handle, const String& dbpath): _server(handle), _riskSystem(nullptr) {
    _broker = handle->GetBrokerSubSystem();
    // Risk system start
    _riskSystem = new RiskSubSystem(handle);
    auto default_config = handle->GetConfig().GetDefault();
    if (default_config.contains("risk")) {
        _riskSystem->Init(default_config["risk"]);
    }
}

TraderSystem::~TraderSystem() {
    if (_riskSystem) {
        delete _riskSystem;
    }
}
void TraderSystem::SetupSimulation(const String& name) {
    _simulations.push_back(name);
}

void TraderSystem::Start() {
    _stock_working_range = GetWorkingRange(ExchangeName::MT_Beijing);

    _simulater_trans = new Transfer([this] (nng_socket from, nng_socket to) {
        DataFeatures dm;
        if (!ReadFeatures(from, dm)) {
            return true;
        }
        if (!_collections.empty()) {
            for (int i = 0; i < dm._features.size(); ++i) {
                auto id =  dm._features[i];
                if (_collections.count(id)) {
                    auto& val = dm._data[i];
                    _collections[id] = val;
                }
            }
        }
        if (_riskSystem) {
            // TODO: risk manage
        }
        // read prediction and check operator
        auto symb = dm._symbol;
        fixed_time_range tr;
        int op = 0;
        if (!_broker->GetNextPrediction(symb, tr, op))
            return true;

        auto current = Now();
        if (tr == current) {
            // thread_local char 
            if (op & (int)ContractOperator::Long) {
                if (tr.IsDaily()) {
                    if (DailyBuy(symb, dm)) {
                        _broker->DoneForecast(symb, op);
                    }
                }
                else {
                    ImmediatelyBuy(symb, dm._price, OrderType::Market);
                }
            }
            if (op & (int)ContractOperator::Sell) {
                if (StrategySell(symb, dm)) {
                    _broker->DoneForecast(symb, op);
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

void TraderSystem::RegistCollection(const String& name) {
    std::unique_lock<std::mutex> lck(_mtx);
    _collections[std::hash<String>()(name)];
}

void TraderSystem::ClearCollections() {
    std::unique_lock<std::mutex> lck(_mtx);
    _collections.clear();
}

const std::variant<float, Vector<float>>& TraderSystem::GetCollection(const String& name) const {
    return _collections.at(std::hash<String>()(name));
}

bool TraderSystem::ImmediatelyBuy(symbol_t symbol, double price, OrderType type) {
    Order order;
    order._side = 0;
    order._type = type;
    order._number = 0;
    order._order[0]._price = price;
    TradeInfo dd;
    if (_broker->Buy(symbol, order, dd) == OrderStatus::All) {
        LOG("buy order: {}, result: {}", order, dd);
        return true;
    }
    return false;
}

bool TraderSystem::DailyBuy(symbol_t symbol, const DataFeatures& features) {
    // TODO: 分批多次入场

    float vwap = -1;
    for (int i = 0; i < features._data.size(); ++i) {
        if (features._features[i] == std::hash<StringView>()(VWAPFeature::name())) {
            vwap = features._data[i];
            break;
        }
    }

    if (features._price < vwap || IsNearClose(symbol)) {
        return ImmediatelyBuy(symbol, features._price, OrderType::Market);
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
        return ImmediatelySell(symbol, features._price, OrderType::Market);
    }
    return false;
}

bool TraderSystem::ImmediatelySell(symbol_t symbol, double price, OrderType type) {
    Order order;
    order._number = 0;
    order._side = 1;
    order._order[0]._price = price;
    TradeInfo dd;
    if (_broker->Sell(symbol, order, dd) == OrderStatus::All) {
        LOG("sell order: {}, result: {}", order, dd);
        // _server->SendEmail("Sell " + get_symbol(symbol) + "[price: " + std::to_string(features._price) + "]");
        return true;
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
