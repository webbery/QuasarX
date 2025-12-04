#include "AgentSubSystem.h"
#include "Metric/Sharp.h"
#include "StrategyNode.h"
#include "server.h"
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
#include "Nodes/SignalNode.h"
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
        if (item.second._worker) {
            item.second._running = false;
            if (item.second._worker->joinable()) item.second._worker->join();
            delete item.second._worker;
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
    if (flow._worker) {
        flow._running = false;
        if (flow._worker->joinable()) flow._worker->join();
        delete flow._worker;
    }
    flow._running = true;
    flow._worker = new std::thread([strategy, this]() {
        DataContext context;
        try {
            RegistIndicator(strategy);

            auto& flow = _flows[strategy];
            uint64_t epoch = 0;
            while (flow._running || !Server::IsExit()) {
                context.SetEpoch(++epoch);
                if (!RunGraph(strategy, flow, context) || context.GetEpoch() == 0) {
                    break;
                }
            }
            // 统计指标
            auto endNode = dynamic_cast<SignalNode*>(flow._graph.back());
            if (endNode) {
                auto& cash_flow = endNode->GetReports();
                flow._collections[StatisticIndicator::Sharp] = sharp_ratio(cash_flow, context, 0);
            }
            // 结束通知
            flow._running = false;
            INFO("backtest finish");
        } catch (const std::invalid_argument& e) {
            WARN("invalid argument error: {}", e.what());
        }
    });
}

Set<symbol_t> FlowSubsystem::GetPools(const String& strategy) {
    auto& flow = _flows[strategy];
    for (auto& node: flow._graph) {
        if (typeid(*node) == typeid(SignalNode)) {
            auto p = (SignalNode*)node;
            return p->GetPool();
        }
    }
}

bool FlowSubsystem::RunGraph(const String& strategy, const StrategyFlowInfo& flow, DataContext& context) {
    for (auto node: flow._graph) {
        if (!node->Process(strategy, context)) {
            return false;
        }
    }
    return true;
}

void FlowSubsystem::RegistIndicator(const String& strategy) {
    auto broker = _handle->GetBrokerSubSystem();
    broker->CleanAllIndicators(strategy);
    broker->RegistIndicator(strategy, StatisticIndicator::VaR);
    broker->RegistIndicator(strategy, StatisticIndicator::Sharp);
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
    auto symb = data._symbols[0];
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
                ImmediatelyBuy(strategy, symb, std::get<double>(data._data.front()), OrderType::Market);
            }
        }
        if (op & (int)ContractOperator::Sell) {
            if (tr.IsDaily()) {
                if (DailySell(strategy, symb, data)) {
                    broker->DoneForecast(symb, op);
                }
            }
            else {
                ImmediatelySell(strategy, symb, std::get<double>(data._data.front()), OrderType::Market);
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
    order._volume = 0;
    order._order[0]._price = price;
    TradeInfo dd;
    auto id = broker->Buy(strategy, symbol, order, dd);
    if (id._id != 0) {
        LOG("buy order: {}, result: {}", order, dd);
        return true;
    }
    return false;
}

bool FlowSubsystem::ImmediatelySell(const String& strategy, symbol_t symbol, double price, OrderType type) {
    auto broker = _handle->GetBrokerSubSystem();
    Order order;
    order._volume = 0;
    order._side = true;
    order._order[0]._price = price;
    TradeInfo dd;
    auto id = broker->Sell(strategy, symbol, order, dd);
    return true;
}

bool FlowSubsystem::GenerateSignal(symbol_t symbol, const DataFeatures& features) {
    float vwap = -1;
    for (int i = 0; i < features._data.size(); ++i) {
        if (features._names[i] == VWAPFeature::name()) {
            vwap = std::get<double>(features._data[i]);
            break;
        }
    }
    return true;
    // return features._price < vwap || IsNearClose(symbol);
}

bool FlowSubsystem::DailyBuy(const String& strategy, symbol_t symbol, const DataFeatures& features) {
    // if (_handle->GetRunningMode() == RuningType::Backtest) {
    //     // 如果是天级数据,则使用收盘价
    //     return ImmediatelyBuy(strategy, symbol, features._price, OrderType::Market);
    // }
    // else {
    //     // TODO: 分批多次入场

    //     if (GenerateSignal(symbol, features)) {
    //         return ImmediatelyBuy(strategy, symbol, features._price, OrderType::Market);
    //     }
    // }
    return false;
}

bool FlowSubsystem::DailySell(const String& strategy, symbol_t symbol, const DataFeatures& features)
{
    if (_handle->GetRunningMode() == RuningType::Backtest) {
        // return ImmediatelySell(strategy, symbol, features._price, OrderType::Market);
    }
    return StrategySell(strategy, symbol, features);
}

bool FlowSubsystem::StrategySell(const String& strategyName, symbol_t symbol, const DataFeatures& features) {
    float vwap = std::numeric_limits<float>::max();
    // for (int i = 0; i < features._data.size(); ++i) {
    //     if (features._features[i] == std::hash<StringView>()(VWAPFeature::name())) {
    //         vwap = features._data[i];
    //         break;
    //     }
    // }
    // if (features._price > vwap || IsNearClose(symbol)) {
    //     return ImmediatelySell(strategyName, symbol, features._price, OrderType::Market);
    // }
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

const Map<StatisticIndicator, std::variant<float, List<float>>>& FlowSubsystem::GetCollection(const String& strategy) const {
    auto itr = _flows.find(strategy);
    if (itr == _flows.end()) {
        WARN("strategy {} not exist", strategy);
    }
    // 等待对应线程完成
    auto& flow = itr->second;
    if (flow._worker && flow._worker->joinable()) {
        flow._worker->join();
    }
    return flow._collections;
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
