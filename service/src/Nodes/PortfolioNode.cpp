#include "Nodes/PortfolioNode.h"
#include "StrategyNode.h"
#include "Util/system.h"
#include "server.h"
#include "Bridge/exchange.h"
#include "Util/log.h"
#include "PortfolioSubsystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Nodes/SignalNode.h"

PortfolioNode::PortfolioNode(Server* server)
    : _server(server)
    , _positionRatio(0.5)
{
}

PortfolioNode::~PortfolioNode() = default;

bool PortfolioNode::Init(const nlohmann::json& config) {
    // 仓位比例
    if (config["params"].contains("positionRatio")) {
        _positionRatio = config["params"]["positionRatio"]["value"];
    }

    // 交易池 - 优先从 config 读取，如果没有则从上游 SignalNode 获取
    if (config["params"].contains("pool")) {
        auto& poolConfig = config["params"]["pool"]["value"];
        for (const String& code : poolConfig) {
            auto& security = Server::GetSecurity(code);
            _pool.emplace_back(to_symbol(code, security));
        }
    } else {
        // 从上游 SignalNode 获取交易池
        for (auto& [port, inputNode] : _ins) {
            if (auto* signalNode = dynamic_cast<SignalNode*>(inputNode)) {
                auto pool = signalNode->GetPool();
                for (const auto& symbol : pool) {
                    _pool.emplace_back(symbol);
                }
            }
        }
    }

    return true;
}

void PortfolioNode::Prepare(const String& strategy, DataContext& context) {
    auto* exchange = (_server->GetAvaliableStockExchange());
    if (_server->GetRunningMode()==RuningType::Backtest) {
        auto broker = dynamic_cast<StockHistorySimulation*>(exchange);
        double initialCapital = broker->GetAvailableFunds(context.getBacktestRunId());
        context.setInitialCapital(initialCapital);
    }
}

NodeProcessResult PortfolioNode::Process(const String& strategy, DataContext& context) {
    // 1. 获取上游节点的信号
    Vector<symbol_t> symbols;
    Vector<TradeAction> actions;

    for (const auto& symbol : _pool) {
        String signalKey = get_symbol(symbol) + ".signal";
        if (context.exist(signalKey)) {
            const auto& signalVal = context.get<Vector<double>>(signalKey);
            auto signal = signalVal.back();

            if (signal > 0) {
                symbols.push_back(symbol);
                actions.push_back(TradeAction::BUY);
            } else if (signal < 0) {
                symbols.push_back(symbol);
                actions.push_back(TradeAction::SELL);
            }
        }
    }

    // 如果没有信号，保持现有仓位
    if (symbols.empty()) {
        symbols = _pool;
        actions.resize(symbols.size(), TradeAction::HOLD);
    }

    // 2. 获取可用资金
    double capital = context.getAvailableCapital();
    double targetCapital = capital * _positionRatio;

    // 3. 生成执行计划
    ExecutionPlan newPlan;
    if (_server->GetRunningMode() != RuningType::Backtest) {
        newPlan = generatePlan(symbols, actions, targetCapital);
    } else {
        newPlan = generatePlan(context, symbols, actions, targetCapital);
    }

    // 4. 检查是否有变化
    newPlan._hasChanged = isPlanChanged(newPlan);

    // 5. 如果有变化或首次执行，输出执行计划
    if (newPlan._hasChanged || _lastPlan._items.empty()) {
        _lastPlan._items = std::move(newPlan._items);
        _lastPlan._totalCapital = newPlan._totalCapital;
        _lastPlan._usedCapital = newPlan._usedCapital;
        _lastPlan._hasChanged = true;

        context.GetExecutionPlan() = _lastPlan;
        // INFO("PortfolioNode: generated new execution plan with {} items, capital={}",
        //      _lastPlan._items.size(), capital);
    } else {
        context.GetExecutionPlan()._hasChanged = false;
        //INFO("PortfolioNode: no change, keeping current positions");
    }

    return NodeProcessResult::Success;
}

ExecutionPlan PortfolioNode::generatePlan(
    const Vector<symbol_t>& symbols,
    const Vector<TradeAction>& actions,
    double targetCapital
) {
    ExecutionPlan plan;
    plan._totalCapital = targetCapital;
    plan._usedCapital = 0.0;
    plan._hasChanged = false;

    if (symbols.empty()) {
        return plan;
    }

    size_t count = symbols.size();
    double perSymbolCapital = targetCapital / count;

    auto* exchange = _server->GetAvaliableStockExchange();

    for (size_t i = 0; i < symbols.size(); ++i) {
        ExecutionItem item;
        item._symbol = symbols[i];
        item._action = actions[i];

        QuoteInfo quote = exchange->GetQuote(item._symbol);
        double price = (quote._close > 0) ? quote._close : quote._open;

        if (price <= 0) {
            WARN("Invalid price for symbol {}", get_symbol(item._symbol));
            continue;
        }

        if (item._action == TradeAction::HOLD) {
            auto& position = _server->GetPosition("");
            for (const auto& pos : position._positions) {
                if (pos._symbol == item._symbol) {
                    item._quantity = static_cast<int>(pos._holds);
                    item._limitPrice = 0;
                    item._targetValue = item._quantity * price;
                    plan._items.push_back(item);
                    plan._usedCapital += item._targetValue;
                    break;
                }
            }
        } else if (item._action == TradeAction::BUY) {
            int quantity = static_cast<int>(perSymbolCapital / price / 100) * 100;
            if (quantity < 100) {
                WARN("Quantity {} too small for symbol {}", quantity, get_symbol(item._symbol));
                continue;
            }
            item._quantity = quantity;
            item._limitPrice = 0;
            item._targetValue = quantity * price;
            plan._items.push_back(item);
            plan._usedCapital += item._targetValue;
        } else if (item._action == TradeAction::SELL) {
            auto& position = _server->GetPosition("");
            int currentQty = 0;
            for (const auto& pos : position._positions) {
                if (pos._symbol == item._symbol) {
                    currentQty = static_cast<int>(pos._holds);
                    break;
                }
            }
            if (currentQty > 0) {
                item._quantity = currentQty;
                item._limitPrice = 0;
                item._targetValue = currentQty * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            }
        }
    }

    return plan;
}

ExecutionPlan PortfolioNode::generatePlan(DataContext& context, const Vector<symbol_t>& symbols,
                              const Vector<TradeAction>& actions,
                              double targetCapital) {
    ExecutionPlan plan;
    plan._totalCapital = targetCapital;
    plan._usedCapital = 0.0;
    plan._hasChanged = false;

    if (symbols.empty()) {
        return plan;
    }

    size_t count = symbols.size();
    double perSymbolCapital = targetCapital / count;

    // 获取历史数据仿真交易所，用于获取未复权价格
    auto* histExchange = dynamic_cast<StockHistorySimulation*>(
        _server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));

    for (size_t i = 0; i < symbols.size(); ++i) {
        ExecutionItem item;
        item._symbol = symbols[i];
        item._action = actions[i];

        String symbolName = get_symbol(item._symbol);

        // 获取未复权价格（原始价格）用于回测交易
        double price = 0.0;
        if (histExchange) {
            price = histExchange->GetPrimitivePrice(item._symbol, context.GetEpoch() - 1);
        }

        if (price <= 0) {
            WARN("Invalid price for symbol {} in backtest context", symbolName);
            continue;
        }

        if (item._action == TradeAction::HOLD) {
            // 保持持仓：从 StockHistorySimulation 获取当前持仓
            int64_t currentQty = 0;
            if (histExchange) {
                currentQty = histExchange->GetPositionQuantity(item._symbol);
            }
            item._quantity = currentQty;
            if (item._quantity > 0) {
                item._limitPrice = 0;
                item._targetValue = item._quantity * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            }
        } else if (item._action == TradeAction::BUY) {
            // 检查是否已有持仓
            int64_t currentQty = 0;
            if (histExchange) {
                currentQty = histExchange->GetPositionQuantity(item._symbol);
            }
            if (currentQty > 0) {
                // 已有持仓，跳过买入
                INFO("Symbol {} already has position {}, skipping buy", symbolName, currentQty);
                continue;
            }
            // 使用未复权价格计算固定买卖数量
            int64_t quantity = static_cast<int64_t>(perSymbolCapital / price / 100) * 100;
            if (quantity < 100) {
                WARN("Quantity {} too small for symbol {}", quantity, symbolName);
                continue;
            }
            item._quantity = quantity;
            item._limitPrice = price;  // 使用未复权收盘价作为限价
            item._targetValue = quantity * price;
            plan._items.push_back(item);
            plan._usedCapital += item._targetValue;
        } else if (item._action == TradeAction::SELL) {
            // 卖出：获取当前持仓
            int64_t currentQty = 0;
            if (histExchange) {
                currentQty = histExchange->GetPositionQuantity(item._symbol);
            }
            if (currentQty > 0) {
                item._quantity = currentQty;
                item._limitPrice = price;  // 使用未复权收盘价作为限价
                item._targetValue = currentQty * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            }
        }
    }

    return plan;
}

bool PortfolioNode::isPlanChanged(const ExecutionPlan& newPlan) {
    if (_lastPlan._items.size() != newPlan._items.size()) {
        return true;
    }

    for (size_t i = 0; i < newPlan._items.size(); ++i) {
        const auto& oldItem = _lastPlan._items[i];
        const auto& newItem = newPlan._items[i];

        if (oldItem._symbol != newItem._symbol ||
            oldItem._action != newItem._action ||
            oldItem._quantity != newItem._quantity) {
            return true;
        }
    }

    return false;
}

const nlohmann::json PortfolioNode::getParams() {
    return {"positionRatio", "pool", "initialCapital"};
}

Map<String, ArgType> PortfolioNode::out_elements() {
    Map<String, ArgType> elems;
    elems["execution_plan"] = ArgType::Integer_TimeSeries;
    return elems;
}
