#include "AgentSubSystem.h"
#include "server.h"
#include "Transfer.h"
#include "Util/system.h"
#include "StrategySubSystem.h"
#include "json.hpp"
#include "Bridge/CTP/CTPSymbol.h"
#include "yas/serialize.hpp"
#include "DataSource.h"
#include <filesystem>
#include "BrokerSubSystem.h"
#include "Strategy/Daily.h"
#include "Agents/XGBoostAgent.h"
#include "Agents/DeepAgent.h"
#include <exception>
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

AgentSubsystem::AgentSubsystem(Server* handle):_handle(handle) {
    _riskSystem = new RiskSubSystem(handle);
    auto default_config = handle->GetConfig().GetDefault();
    if (default_config.contains("risk")) {
        _riskSystem->Init(default_config["risk"]);
    }
    _stock_working_range = GetWorkingRange(ExchangeName::MT_Beijing);
}

AgentSubsystem::~AgentSubsystem() {
    for (auto& item : _pipelines) {
        if (item.second._transfer) {
            item.second._transfer->stop();
            delete item.second._transfer;
        }
        if (item.second._agent) {
            delete item.second._agent;
        }

    }
    _pipelines.clear();
    delete _riskSystem;
}

bool AgentSubsystem::LoadConfig(const AgentStrategyInfo& config) {
    bool status = true;
    auto& setting = _pipelines[config._name];
    if (setting._transfer) {
        setting._transfer->stop();
    }
    if (setting._agent) {
        delete setting._agent;
    }
    if (setting._strategy) {
        delete setting._strategy;
    }
    setting._future = config._future;
    // 约定最终输出端口名为策略名
    for (auto& agent: config._agents) {
        String model_path = String("models/").append(agent._modelpath);
        if (!std::filesystem::exists(model_path)) {
            WARN("model `{}` not exist", model_path);
            status = false;
            continue;
        }
        switch(agent._type) {
        case AgentType::XGBoost:
            setting._agent = new XGBoostAgent(model_path, agent._classes, agent._params);
        break;
        case AgentType::NeuralNetwork:
            setting._agent = NerualNetworkAgentManager::GetInstance().GenerateAgent(model_path, agent._params);
        break;
        case AgentType::LinearRegression:
        break;
        default:
        WARN("can not create agent of type: {}", (int)agent._type);
        break;
        }
    }
    nlohmann::json params;
    setting._strategy = new TestStrategy();
    switch (config._strategy) {
    case StrategyType::ST_InterDay:
        setting._strategy->setT0(false);
    break;
    case StrategyType::ST_IntraDay:
        setting._strategy->setT0(true);
    break;
    default:
        WARN("no support strategy {}", (int)config._strategy);
    break;
    }
    if (setting._transfer) {
        setting._transfer->start(config._name.data(), URI_FEATURE);
    }
    return status;
}

void AgentSubsystem::Start() {
    auto broker = _handle->GetBrokerSubSystem();
    for (auto& item : _pipelines) {
        auto name = item.first;
        item.second._transfer = new Transfer([&item, broker, this](nng_socket& from, nng_socket& to) {
            // if (strategy && !strategy->is_valid())
            //     return true;

            constexpr std::size_t flags = yas::mem | yas::binary;
            size_t sz = 0;
            char* buff = nullptr;
            int rv = nng_recv(from, &buff, &sz, NNG_FLAG_ALLOC);
            if (rv != 0) {
                nng_free(buff, sz);
                return true;
            }
            yas::shared_buffer buf;
            buf.assign(buff, sz);
            DataFeatures messenger;
            yas::load<flags>(buf, messenger);
            nng_free(buff, sz);

            if (_handle->GetRunningMode() != RuningType::Backtest) {
                if (item.second._future > 0 && _handle->IsOpen(messenger._symbol, Now())) {
                    return true;
                }
            }
            
            // TODO: risk manage
            _riskSystem->Metric(messenger);
            try {
                if (_handle->GetRunningMode() == RuningType::Backtest) {
                    RunBacktest(item.second._strategy, messenger);
                } else {
                    RunInstant(item.second._strategy, messenger);
                }
            } catch (const std::exception& e) {
                FATAL("{}", e.what());
            }
            return true;
            });
        item.second._transfer->start(name.data(), URI_FEATURE);
    }
}

void AgentSubsystem::RunBacktest(QStrategy* strategy, const DataFeatures& input) {
    if (strategy->isT0()) {

    }
    else {
        ProcessToday(input);
        PredictTomorrow(strategy, input);
    }
}

void AgentSubsystem::RunInstant(QStrategy* strategy, const DataFeatures& input) {
    // process feature(daily or second)
    auto result = strategy->Process(input._data);
    if (strategy->isT0()) {

    }
    else {
        
    }
}

void AgentSubsystem::ProcessToday(const DataFeatures& data) {
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
                if (DailyBuy(symb, data)) {
                    broker->DoneForecast(symb, op);
                }
            }
            else {
                ImmediatelyBuy(symb, data._price, OrderType::Market);
            }
        }
        if (op & (int)ContractOperator::Sell) {
            if (tr.IsDaily()) {
                if (DailySell(symb, data)) {
                    broker->DoneForecast(symb, op);
                }
            }
            else {
                ImmediatelySell(symb, data._price, OrderType::Market);
            }
        }
        if (op & (int)ContractOperator::Short) {
            DEBUG_INFO("Short");
        }
    }
}

void AgentSubsystem::PredictTomorrow(QStrategy* strategy, const DataFeatures& input) {
    auto result = strategy->Process(input._data);
    auto broker = _handle->GetBrokerSubSystem();
    auto value = std::get<double>(result);
    int op = value > 0.5? (int)ContractOperator::Buy : (int)ContractOperator::Sell;
    broker->PredictWithDays(input._symbol, 1, op);
}

bool AgentSubsystem::ImmediatelyBuy(symbol_t symbol, double price, OrderType type) {
    auto broker = _handle->GetBrokerSubSystem();
    Order order;
    order._time = Now();
    order._side = 0;
    order._type = type;
    order._number = 0;
    order._order[0]._price = price;
    TradeInfo dd;
    if (broker->Buy(symbol, order, dd) == OrderStatus::All) {
        LOG("buy order: {}, result: {}", order, dd);
        return true;
    }
    return false;
}

bool AgentSubsystem::ImmediatelySell(symbol_t symbol, double price, OrderType type) {
    auto broker = _handle->GetBrokerSubSystem();
    Order order;
    order._number = 0;
    order._side = 1;
    order._order[0]._price = price;
    TradeInfo dd;
    if (broker->Sell(symbol, order, dd) == OrderStatus::All) {
        LOG("sell order: {}, result: {}", order, dd);
        // _server->SendEmail("Sell " + get_symbol(symbol) + "[price: " + std::to_string(features._price) + "]");
        return true;
    }
    return false;
}

bool AgentSubsystem::DailyBuy(symbol_t symbol, const DataFeatures& features) {
    if (_handle->GetRunningMode() == RuningType::Backtest) {
        // 如果是天级数据,则使用收盘价
        return ImmediatelyBuy(symbol, features._price, OrderType::Market);
    }
    else {
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
    }
    return false;
}

bool AgentSubsystem::DailySell(symbol_t symbol, const DataFeatures& features)
{
    if (_handle->GetRunningMode() == RuningType::Backtest) {
        return ImmediatelySell(symbol, features._price, OrderType::Market);
    }
    else {
        return StrategySell(symbol, features);
    }
}

bool AgentSubsystem::StrategySell(symbol_t symbol, const DataFeatures& features) {
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

bool AgentSubsystem::IsNearClose(symbol_t symb) {
    auto current = Now();
    auto name = GetExchangeName(get_symbol(symb));
    auto final_time = *_stock_working_range.rbegin();
    if (current == final_time && final_time.near_end(current)) {
        INFO("force buy because near trade end.");
        return true;
    }
    return false;
}

const Map<String, std::variant<float, List<float>>>& AgentSubsystem::GetCollection(const String& strategy) const {
    auto itr = _pipelines.find(strategy);
    if (itr == _pipelines.end()) {
        WARN("strategy {} not exist", strategy);
    }
    return itr->second._collections;
}

void AgentSubsystem::Train(const String& strategy) {
    // auto& pipeline = _pipelines[strategy];
    // pipeline._transfer = new Transfer([agent = pipeline._agent] (nng_socket from, nng_socket to) {
    //     constexpr std::size_t flags = yas::mem | yas::binary;
    //     size_t sz = 0;
    //     char* buff = nullptr;
    //     int rv = nng_recv(from, &buff, &sz, NNG_FLAG_ALLOC);
    //     if (rv != 0) {
    //         nng_free(buff, sz);
    //         return true;
    //     }
    //     yas::shared_buffer buf;
    //     buf.assign(buff, sz);
    //     DataFeatures messenger;
    //     yas::load<flags>(buf, messenger);
    //     nng_free(buff, sz);
    //     return false;
    // });
    // pipeline._transfer->start(, , );
}

void AgentSubsystem::Create(const String& strategy, AgentType type, const nlohmann::json& params) {
    if (_pipelines.count(strategy)) {
        return;
    }
    // 默认为模拟盘
    auto& pipeline = _pipelines[strategy];
    switch (type) {
    case AgentType::XGBoost:
        // pipeline._agent = new XGBoostAgent(params, "");
    break;
    default:
    break;
    }
}
