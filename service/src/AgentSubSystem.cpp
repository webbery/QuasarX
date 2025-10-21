#include "AgentSubSystem.h"
#include "StrategyNode.h"
#include "server.h"
#include "Transfer.h"
#include "Util/system.h"
#include "StrategySubSystem.h"
#include "json.hpp"
#include "Bridge/CTP/CTPSymbol.h"
#include "std_header.h"
#include "yas/serialize.hpp"
#include "DataSource.h"
#include <filesystem>
#include "BrokerSubSystem.h"
#include "Strategy/Daily.h"
#include "Agents/XGBoostAgent.h"
#include "Agents/DeepAgent.h"
#include <exception>
#include <stdexcept>
#include "RiskSubSystem.h"
#include "Features/VWAP.h"

namespace {
    class TestStrategy: public QStrategy {
    public:
        feature_t Process(const feature_t& input) {
            if (_v < 0.5) {
                _v = 1.0;
            } else {
                _v = 0.0;
            }
            return _v;
        }
    private:
        double _v = 0.;
    };
}

FlowSubsystem::FlowSubsystem(Server* handle):_handle(handle) {
    _riskSystem = new RiskSubSystem(handle);
    auto default_config = handle->GetConfig().GetDefault();
    if (default_config.contains("risk")) {
        _riskSystem->Init(default_config["risk"]);
    }
    _stock_working_range = GetWorkingRange(ExchangeName::MT_Beijing);
}

FlowSubsystem::~FlowSubsystem() {
    for (auto& item : _flows) {
        if (item.second._transfer) {
            item.second._transfer->stop();
            delete item.second._transfer;
        }
        for (auto node: item.second._graph) {
            delete node;
        }
    }
    _flows.clear();

    delete _riskSystem;
}

bool FlowSubsystem::LoadFlow(const String& strategy, const List<QNode*>& topo_flow) {
    bool status = true;
    _flows[strategy]._graph = topo_flow;
    return status;
}

void FlowSubsystem::Start() {
    auto broker = _handle->GetBrokerSubSystem();
    for (auto& item : _flows) {
        auto name = item.first;
        Start(name);
    }
}

void FlowSubsystem::Start(const String& strategy) {
    auto& flow = _flows.at(strategy);
    if (flow._transfer) {
        delete flow._transfer;
    }
    DataContext context;
    flow._transfer = new Transfer([context{std::move(context)}, strategy, this](nng_socket& from, nng_socket& to) mutable {
        DataFeatures messenger;
        if (!ReadFeatures(from, messenger)) {
            return true;
        }
        auto& flow = _flows[strategy];
        if (_handle->GetRunningMode() != RuningType::Backtest) {
            if (flow._future > 0 && _handle->IsOpen(messenger._symbol, Now())) {
                return true;
            }
        }
        
        try {
            for (auto node: flow._graph) {
                if (!node->Process(context, messenger)) {
                    return false;
                }
            }
        } catch (const std::invalid_argument& e) {
            WARN("invalid argument error: {}", e.what());
            return false;
        }
        
        return true;
    });
    flow._transfer->start(strategy, URI_FEATURE);
}

void FlowSubsystem::RunBacktest(const String& strategyName, QStrategy* strategy, const DataFeatures& input) {
    if (strategy->isT0()) {

    }
    else {
        ProcessToday(strategyName, input);
        PredictTomorrow(strategyName, strategy, input);
    }
}

void FlowSubsystem::RunInstant(const String& strategyName, QStrategy* strategy, const DataFeatures& input) {
    // process feature(daily or second)
    //auto result = strategy->Process(input._data);
    if (strategy->isT0()) {

    }
    else {
        
    }
}

void FlowSubsystem::ProcessToday(const String& strategy, const DataFeatures& data) {
    auto broker = _handle->GetBrokerSubSystem();
    // 如果是daily，那么在第二天操作
    auto symb = data._symbol;
    fixed_time_range tr;
    int op = 0;
    if (!broker->GetNextPrediction(symb, tr, op))
        return;

    auto current = Now();
    if (tr == current) {
        // thread_local char 
        if (op & (int)ContractOperator::Buy) {
            if (tr.IsDaily()) {
                if (DailyBuy(strategy, symb, data)) {
                    broker->DoneForecast(symb, op);
                }
            }
            else {
                ImmediatelyBuy(strategy, symb, data._price, OrderType::Market);
            }
        }
        if (op & (int)ContractOperator::Sell) {
            if (tr.IsDaily()) {
                if (DailySell(strategy, symb, data)) {
                    broker->DoneForecast(symb, op);
                }
            }
            else {
                ImmediatelySell(strategy, symb, data._price, OrderType::Market);
            }
        }
        if (op & (int)ContractOperator::Short) {
            DEBUG_INFO("Short");
        }
    }
}

void FlowSubsystem::PredictTomorrow(const String& strategyName, QStrategy* strategy, const DataFeatures& input) {
    //auto result = strategy->Process(input._data);
    auto broker = _handle->GetBrokerSubSystem();
    //auto value = std::get<double>(result);
    //int op = value > 0.5? (int)ContractOperator::Buy : (int)ContractOperator::Sell;
    //broker->PredictWithDays(input._symbol, 1, op);
}

bool FlowSubsystem::ImmediatelyBuy(const String& strategy, symbol_t symbol, double price, OrderType type) {
    auto broker = _handle->GetBrokerSubSystem();
    Order order;
    order._time = Now();
    order._side = 0;
    order._type = type;
    order._number = 0;
    order._order[0]._price = price;
    TradeInfo dd;
    if (broker->Buy(strategy, symbol, order, dd) == OrderStatus::All) {
        LOG("buy order: {}, result: {}", order, dd);
        return true;
    }
    return false;
}

bool FlowSubsystem::ImmediatelySell(const String& strategy, symbol_t symbol, double price, OrderType type) {
    auto broker = _handle->GetBrokerSubSystem();
    Order order;
    order._number = 0;
    order._side = 1;
    order._order[0]._price = price;
    TradeInfo dd;
    if (broker->Sell(strategy, symbol, order, dd) == OrderStatus::All) {
        LOG("sell order: {}, result: {}", order, dd);
        // _server->SendEmail("Sell " + get_symbol(symbol) + "[price: " + std::to_string(features._price) + "]");
        return true;
    }
    return false;
}

bool FlowSubsystem::GenerateSignal(symbol_t symbol, const DataFeatures& features) {
    float vwap = -1;
    for (int i = 0; i < features._data.size(); ++i) {
        if (features._features[i] == std::hash<StringView>()(VWAPFeature::name())) {
            vwap = features._data[i];
            break;
        }
    }
    return features._price < vwap || IsNearClose(symbol);
}

bool FlowSubsystem::DailyBuy(const String& strategy, symbol_t symbol, const DataFeatures& features) {
    if (_handle->GetRunningMode() == RuningType::Backtest) {
        // 如果是天级数据,则使用收盘价
        return ImmediatelyBuy(strategy, symbol, features._price, OrderType::Market);
    }
    else {
        // TODO: 分批多次入场

        if (GenerateSignal(symbol, features)) {
            return ImmediatelyBuy(strategy, symbol, features._price, OrderType::Market);
        }
    }
    return false;
}

bool FlowSubsystem::DailySell(const String& strategy, symbol_t symbol, const DataFeatures& features)
{
    if (_handle->GetRunningMode() == RuningType::Backtest) {
        return ImmediatelySell(strategy, symbol, features._price, OrderType::Market);
    }
    else {
        return StrategySell(strategy, symbol, features);
    }
}

bool FlowSubsystem::StrategySell(const String& strategyName, symbol_t symbol, const DataFeatures& features) {
    float vwap = std::numeric_limits<float>::max();
    for (int i = 0; i < features._data.size(); ++i) {
        if (features._features[i] == std::hash<StringView>()(VWAPFeature::name())) {
            vwap = features._data[i];
            break;
        }
    }
    if (features._price > vwap || IsNearClose(symbol)) {
        return ImmediatelySell(strategyName, symbol, features._price, OrderType::Market);
    }
    return false;
}

bool FlowSubsystem::IsNearClose(symbol_t symb) {
    auto current = Now();
    auto name = GetExchangeName(get_symbol(symb));
    auto final_time = *_stock_working_range.rbegin();
    if (current == final_time && final_time.near_end(current)) {
        INFO("force buy because near trade end.");
        return true;
    }
    return false;
}

const Map<String, std::variant<float, List<float>>>& FlowSubsystem::GetCollection(const String& strategy) const {
    auto itr = _flows.find(strategy);
    if (itr == _flows.end()) {
        WARN("strategy {} not exist", strategy);
    }
    return itr->second._collections;
}

void FlowSubsystem::Create(const String& strategy, SignalGeneratorType type, const nlohmann::json& params) {
    if (_flows.count(strategy)) {
        return;
    }
    // 默认为模拟盘
    auto& pipeline = _flows[strategy];
    switch (type) {
    case SignalGeneratorType::XGBoost:
        // pipeline._agent = new XGBoostAgent(params, "");
    break;
    default:
    break;
    }
}
