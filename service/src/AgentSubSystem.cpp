#include "AgentSubSystem.h"
#include "Bridge/exchange.h"
#include "Nodes/QuoteNode.h"
#include "StrategyNode.h"
#include "server.h"
#include "Util/system.h"
#include "StrategySubSystem.h"
#include "json.hpp"
#include "Bridge/CTP/CTPSymbol.h"
#include "yas/serialize.hpp"
#include "BrokerSubSystem.h"
#include "Nodes/SignalNode.h"
#include <exception>
#include <stdexcept>
#include "RiskSubSystem.h"
#include "Nodes/ScriptNode.h"
#include "Nodes/ExecuteNode.h"
#include "Bridge/SIM/StockHistorySimulation.h"

#include "Metric/Return.h"
#include "Metric/Sharp.h"
#include "Metric/Drawdown.h"

FlowSubsystem::FlowSubsystem(Server* handle):_handle(handle) {
    auto default_config = handle->GetConfig().GetDefault();
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
}

bool FlowSubsystem::LoadFlow(const String& strategy, const List<QNode*>& topo_flow) {
    bool status = true;
    _flows[strategy]._graph = topo_flow;
    return status;
}

void FlowSubsystem::SetStrategyConfig(const String& strategy, const nlohmann::json& config) {
    _flows[strategy]._config = config;
}

void FlowSubsystem::ClearFlow(const String& strategy) {
    for (auto node: _flows[strategy]._graph) {
        delete node;
    }
    _flows[strategy]._graph.clear();
}

void FlowSubsystem::Start() {
    auto broker = _handle->GetBrokerSubSystem();
    for (auto& item : _flows) {
        auto name = item.first;
        Start(name);
    }
}

void FlowSubsystem::Start(const String& strategy) {
    Stop(strategy);
    auto& flow = _flows.at(strategy);
    flow._running = true;
    flow._worker = new std::thread([strategy, this]() {
        DataContext context(strategy, _handle);

        try {
            auto& flow = _flows[strategy];
            if (IsUseShareMemory(flow)) {
                context.EnableShareMemory(strategy);
            }
            for (auto node : flow._graph) {
                node->Prepare(strategy, context);
            }

            uint64_t epoch = 0;
            bool success = true;
            auto startTick = std::chrono::high_resolution_clock::now();
            while (flow._running || !Server::IsExit()) {
                context.SetEpoch(++epoch);
                if (!RunGraph(strategy, flow, context)) {
                    success = false;
                    break;
                }
                // 检查是否数据已用完（QuoteInputNode 正常退出）
                if (context.GetEpoch() == 0) {
                    break;
                }
            }
            auto endTick = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(endTick - startTick);

            if (success) {
                // 统计指标
                ExecuteNode* endNode = nullptr;
                for (auto node: flow._graph) {
                    auto n = dynamic_cast<ExecuteNode*>(node);
                    if (n) {
                        endNode = n;
                        break;
                    }
                }
                if (endNode) {
                    auto& cash_flow = endNode->GetReports();
                    flow._collections[StatisticIndicator::Sharp] = sharp_ratio(cash_flow, context, 0);
                    flow._collections[StatisticIndicator::AnualReturn] = annual_return_ratio(cash_flow, context);
                    flow._collections[StatisticIndicator::TotalReturn] = total_return_ratio(cash_flow, context);
                    flow._collections[StatisticIndicator::MaxDrawDown] = max_drawdown_ratio(cash_flow, context);
                    flow._collections[StatisticIndicator::WinRate] = win_rate(cash_flow, context);
                    flow._collections[StatisticIndicator::Calmar] = calmar_ratio(cash_flow, context, 0);
                }
                for (auto node : flow._graph) {
                    node->Done(strategy);
                }
            }
            // 回测失败时需要保证QuoteInputNode正常退出
            if (_handle->GetRunningMode() == RuningType::Backtest) {
                auto broker = _handle->GetAvaliableStockExchange();
                broker->Logout();
            }
            // 结束通知
            flow._running = false;
            auto info = fmt::format("backtest finish, cost {}s, {}ms/per datum", duration.count(), (epoch == 0? 0:duration.count()*1000.0/epoch));
            strategy_log(strategy, info);
        } catch (const std::invalid_argument& e) {
            WARN("invalid argument error: {}", e.what());
        }
    });
}

void FlowSubsystem::Stop(const String& strategy) {
    auto& flow = _flows.at(strategy);
    if (flow._worker) {
        flow._running = false;
        if (flow._worker->joinable()) flow._worker->join();
        delete flow._worker;
    }
    flow._worker = nullptr;
}

void FlowSubsystem::StartBacktest(const String& strategy, const Set<symbol_t>& symbols, double initialCapital) {
    Stop(strategy);

    // 获取交易所并创建回测上下文
    auto* exchange = dynamic_cast<StockHistorySimulation*>(_handle->GetAvaliableStockExchange());
    if (!exchange) {
        WARN("Failed to get stock exchange for backtest");
        return;
    }

    // 创建回测上下文
    uint16_t runId = exchange->createBacktestContext(strategy, symbols, initialCapital);

    auto& flow = _flows.at(strategy);
    flow._running = true;
    flow._backtestRunId = runId;
    flow._worker = new std::thread([strategy, runId, this]() {
        DataContext context(strategy, _handle);
        context.setBacktestRunId(runId);

        // 获取回测上下文
        auto* exchange = dynamic_cast<StockHistorySimulation*>(_handle->GetAvaliableStockExchange());
        BacktestContext* btContext = exchange ? exchange->getBacktestContext(runId) : nullptr;

        try {
            auto& flow = _flows[strategy];
            if (IsUseShareMemory(flow)) {
                context.EnableShareMemory(strategy);
            }
            for (auto node : flow._graph) {
                node->Prepare(strategy, context);
            }

            uint64_t epoch = 0;
            bool success = true;
            auto startTick = std::chrono::high_resolution_clock::now();

            // 使用 stepForward 推进回测时间
            while (flow._running || !Server::IsExit()) {
                context.SetEpoch(++epoch);

                // 推进回测时间
                if (btContext && !exchange->stepForward(btContext)) {
                    // 数据用完
                    INFO("Backtest data finished for strategy {}", strategy);
                    break;
                }

                if (!RunGraph(strategy, flow, context)) {
                    success = false;
                    break;
                }
            }

            auto endTick = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(endTick - startTick);

            if (success) {
                // 统计指标
                ExecuteNode* endNode = nullptr;
                for (auto node: flow._graph) {
                    auto n = dynamic_cast<ExecuteNode*>(node);
                    if (n) {
                        endNode = n;
                        break;
                    }
                }
                if (endNode) {
                    auto& cash_flow = endNode->GetReports();
                    flow._collections[StatisticIndicator::Sharp] = sharp_ratio(cash_flow, context, 0);
                    flow._collections[StatisticIndicator::AnualReturn] = annual_return_ratio(cash_flow, context);
                    flow._collections[StatisticIndicator::TotalReturn] = total_return_ratio(cash_flow, context);
                    flow._collections[StatisticIndicator::MaxDrawDown] = max_drawdown_ratio(cash_flow, context);
                    flow._collections[StatisticIndicator::WinRate] = win_rate(cash_flow, context);
                    flow._collections[StatisticIndicator::Calmar] = calmar_ratio(cash_flow, context, 0);
                }
                for (auto node : flow._graph) {
                    node->Done(strategy);
                }
            }

            // 回测结束后登出
            if (_handle->GetRunningMode() == RuningType::Backtest) {
                auto broker = _handle->GetAvaliableStockExchange();
                broker->Logout();
            }

            flow._running = false;
            auto info = fmt::format("backtest finish, cost {}s, {}ms/per datum", duration.count(), (epoch == 0? 0:duration.count()*1000.0/epoch));
            strategy_log(strategy, info);
        } catch (const std::invalid_argument& e) {
            WARN("invalid argument error: {}", e.what());
        }

        // 清理回测上下文
        if (exchange) {
            exchange->destroyBacktestContext(runId);
        }
    });
}

bool FlowSubsystem::IsUseShareMemory(const StrategyFlowInfo& flow) {
    auto& graph = flow._graph;
    for (auto node: graph) {
        if (dynamic_cast<ScriptNode*>(node)) {
            return true;
        }
    }
    return false;
}

bool FlowSubsystem::IsRunning(const String& strategy) const {
    auto itr = _flows.find(strategy);
    if (itr == _flows.end()) {
        return false;
    }
    const auto& flow = itr->second;
    return flow._running.load() || (flow._worker && flow._worker->joinable());
}

Set<symbol_t> FlowSubsystem::GetPools(const String& strategy) {
    auto& flow = _flows[strategy];
    for (auto& node: flow._graph) {
        if (typeid(*node) == typeid(SignalNode)) {
            auto p = (SignalNode*)node;
            return p->GetPool();
        }
    }
    return Set<symbol_t>();
}

bool FlowSubsystem::RunGraph(const String& strategy, const StrategyFlowInfo& flow, DataContext& context) {
    // 根据策略图生成信号
    for (auto node: flow._graph) {
        if (!node->Process(strategy, context)) {
            // 回测模式下，如果是 QuoteInputNode 因数据用完返回 false，属于正常退出
            if (_handle->GetRunningMode() == RuningType::Backtest) {
                if (auto quoteNode = dynamic_cast<QuoteInputNode*>(node)) {
                    INFO("{} data finished, backtest completed normally", node->id());
                    // 设置 epoch 为 0，通知外层循环退出
                    context.SetEpoch(0);
                    return true;
                }
            }
            INFO("{} process fail", node->id());
            return false;
        }
    }

    // 调用风控检查 - 通过 Server 获取 RiskSubSystem
    auto risk = _handle->GetRiskSubSystem();
    if (risk) {
        risk->Metric(context);
    }
    return true;
}

void FlowSubsystem::RegistIndicator(const String& strategy) {
    auto broker = _handle->GetBrokerSubSystem();
    broker->CleanAllIndicators(strategy);
    broker->RegistIndicator(strategy, StatisticIndicator::VaR);
    broker->RegistIndicator(strategy, StatisticIndicator::Sharp);
}

// void FlowSubsystem::RunBacktest(const String& strategyName, QStrategy* strategy, const DataFeatures& input) {
//     if (strategy->isT0()) {

//     }
//     else {
//         ProcessToday(strategyName, input);
//         PredictTomorrow(strategyName, strategy, input);
//     }
// }

// void FlowSubsystem::RunInstant(const String& strategyName, QStrategy* strategy, const DataFeatures& input) {
//     // process feature(daily or second)
//     //auto result = strategy->Process(input._data);
//     if (strategy->isT0()) {

//     }
//     else {
        
//     }
// }

// void FlowSubsystem::ProcessToday(const String& strategy, const DataFeatures& data) {
//     auto broker = _handle->GetBrokerSubSystem();
//     // 如果是daily，那么在第二天操作
//     auto symb = data._symbols[0];
//     fixed_time_range tr;
//     int op = 0;
//     if (!broker->GetNextPrediction(symb, tr, op))
//         return;

//     auto current = Now();
//     if (tr == current) {
//         // thread_local char 
//         if (op & (int)ContractOperator::Buy) {
//             if (tr.IsDaily()) {
//                 if (DailyBuy(strategy, symb, data)) {
//                     broker->DoneForecast(symb, op);
//                 }
//             }
//             else {
//                 ImmediatelyBuy(strategy, symb, std::get<double>(data._data.front()), OrderType::Market);
//             }
//         }
//         if (op & (int)ContractOperator::Sell) {
//             if (tr.IsDaily()) {
//                 if (DailySell(strategy, symb, data)) {
//                     broker->DoneForecast(symb, op);
//                 }
//             }
//             else {
//                 ImmediatelySell(strategy, symb, std::get<double>(data._data.front()), OrderType::Market);
//             }
//         }
//         if (op & (int)ContractOperator::Short) {
//             DEBUG_INFO("Short");
//         }
//     }
// }

// void FlowSubsystem::PredictTomorrow(const String& strategyName, QStrategy* strategy, const DataFeatures& input) {
//     //auto result = strategy->Process(input._data);
//     auto broker = _handle->GetBrokerSubSystem();
//     //auto value = std::get<double>(result);
//     //int op = value > 0.5? (int)ContractOperator::Buy : (int)ContractOperator::Sell;
//     //broker->PredictWithDays(input._symbol, 1, op);
// }

bool FlowSubsystem::ImmediatelyBuy(const String& strategy, symbol_t symbol, double price, OrderType type) {
    auto broker = _handle->GetBrokerSubSystem();
    Order order;
    order._time = Now();
    order._side = 0;
    order._type = type;
    order._volume = 0;
    order._price = price;
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
    order._price = price;
    TradeInfo dd;
    auto id = broker->Sell(strategy, symbol, order, dd);
    return true;
}

// bool FlowSubsystem::GenerateSignal(symbol_t symbol, const DataFeatures& features) {
//     float vwap = -1;
    // for (int i = 0; i < features._data.size(); ++i) {
    //     if (features._names[i] == VWAPFeature::name()) {
    //         vwap = std::get<double>(features._data[i]);
    //         break;
    //     }
    // }
    // return features._price < vwap || IsNearClose(symbol);
// }

// bool FlowSubsystem::DailyBuy(const String& strategy, symbol_t symbol, const DataFeatures& features) {
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
    // return false;
// }

// bool FlowSubsystem::DailySell(const String& strategy, symbol_t symbol, const DataFeatures& features)
// {
//     if (_handle->GetRunningMode() == RuningType::Backtest) {
//         // return ImmediatelySell(strategy, symbol, features._price, OrderType::Market);
//     }
//     return StrategySell(strategy, symbol, features);
// }

// bool FlowSubsystem::StrategySell(const String& strategyName, symbol_t symbol, const DataFeatures& features) {
    // float vwap = std::numeric_limits<float>::max();
    // for (int i = 0; i < features._data.size(); ++i) {
    //     if (features._features[i] == std::hash<StringView>()(VWAPFeature::name())) {
    //         vwap = features._data[i];
    //         break;
    //     }
    // }
    // if (features._price > vwap || IsNearClose(symbol)) {
    //     return ImmediatelySell(strategyName, symbol, features._price, OrderType::Market);
    // }
//     return false;
// }

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

// void FlowSubsystem::Create(const String& strategy, SignalGeneratorType type, const nlohmann::json& params) {
//     if (_flows.count(strategy)) {
//         return;
//     }
//     // 默认为模拟盘
//     auto& pipeline = _flows[strategy];
//     switch (type) {
//     case SignalGeneratorType::XGBoost:
//         // pipeline._agent = new XGBoostAgent(params, "");
//     break;
//     default:
//     break;
//     }
// }
