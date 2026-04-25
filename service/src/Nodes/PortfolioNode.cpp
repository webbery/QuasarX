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

    // 是否允许做空
    if (config["params"].contains("allowShort")) {
        _allowShort = config["params"]["allowShort"]["value"];
    }

    // 交易池 - 优先从 config 读取，如果没有则从上游 SignalNode 获取
    if (config["params"].contains("pool")) {
        auto& poolConfig = config["params"]["pool"]["value"];
        for (const String& code : poolConfig) {
            auto& security = Server::GetSecurity(code);
            _pool.insert(to_symbol(code, security));
        }
    } else {
        // 从上游 SignalNode 获取交易池
        for (auto& [port, inputNode] : _ins) {
            if (auto* signalNode = dynamic_cast<SignalNode*>(inputNode)) {
                auto pool = signalNode->GetPool();
                for (const auto& symbol : pool) {
                    _pool.insert(symbol);
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
        symbols.assign(_pool.begin(), _pool.end());
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
    } else {
        context.GetExecutionPlan()._hasChanged = false;
    }

    // 6. 保存执行计划摘要到 context（每次 Process 都写入，确保数据完整）
    // 格式: {symbol}.plan_action (1=BUY, -1=SELL, 0=HOLD), {symbol}.plan_qty, {symbol}.plan_price
    // 构建当前 bar 所有 symbol 的 action/qty/price 映射
    std::map<symbol_t, std::tuple<int, int64_t, double>> symData;
    // 先初始化所有 symbol 为 HOLD
    for (const auto& sym : _pool) {
        symData[sym] = {0, 0, 0.0};
    }
    // 填充有计划的 symbol
    for (const auto& item : _lastPlan._items) {
        int actionVal = 0;
        switch (item._action) {
            case TradeAction::BUY:  actionVal = 1; break;
            case TradeAction::SELL: actionVal = -1; break;
            case TradeAction::HOLD: actionVal = 0; break;
        }
        double price = (item._limitPrice > 0) ? item._limitPrice : 0.0;
        symData[item._symbol] = {actionVal, item._quantity, price};
    }
    // 写入 context
    auto& times = context.GetTime();
    size_t idx = times.empty() ? 0 : times.size() - 1;
    for (const auto& [sym, data] : symData) {
        String symStr = get_symbol(sym);
        auto [actionVal, qty, price] = data;

        String actionKey = symStr + ".plan_action";
        String qtyKey = symStr + ".plan_qty";
        String priceKey = symStr + ".plan_price";

        if (context.exist(actionKey)) {
            context.add(actionKey, (double)actionVal);
            context.add(qtyKey, (double)qty);
            context.add(priceKey, price);
        } else {
            // 首次写入，填充之前的值为 0
            Vector<double> actionVec(idx, 0.0);
            Vector<double> qtyVec(idx, 0.0);
            Vector<double> priceVec(idx, 0.0);
            actionVec.push_back((double)actionVal);
            qtyVec.push_back((double)qty);
            priceVec.push_back(price);
            context.set(actionKey, std::move(actionVec));
            context.set(qtyKey, std::move(qtyVec));
            context.set(priceKey, std::move(priceVec));
        }
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
            auto& position = _server->GetPosition("");
            int currentQty = 0;
            for (const auto& pos : position._positions) {
                if (pos._symbol == item._symbol) {
                    currentQty = static_cast<int>(pos._holds);
                    break;
                }
            }
            if (currentQty < 0) {
                // 有空仓，BUY = 平空
                item._quantity = -currentQty;
                item._flag = 1; // 平仓
                item._limitPrice = 0;
                item._targetValue = item._quantity * price;
                plan._items.push_back(item);
            } else if (currentQty > 0) {
                // 已有多仓，跳过
                continue;
            } else {
                // 无仓，BUY = 开多
                int quantity = static_cast<int>(perSymbolCapital / price / 100) * 100;
                if (quantity < 100) {
                    WARN("Quantity {} too small for symbol {}", quantity, get_symbol(item._symbol));
                    continue;
                }
                item._quantity = quantity;
                item._flag = 0; // 开仓
                item._limitPrice = 0;
                item._targetValue = quantity * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            }
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
                // 平多
                item._quantity = currentQty;
                item._flag = 1; // 平仓
                item._limitPrice = 0;
                item._targetValue = currentQty * price;
                plan._items.push_back(item);
            } else if (currentQty == 0 && _allowShort) {
                // 做空（开仓）
                int quantity = static_cast<int>(perSymbolCapital / price / 100) * 100;
                if (quantity >= 100) {
                    item._quantity = quantity;
                    item._flag = 0; // 开仓
                    item._limitPrice = 0;
                    item._targetValue = quantity * price;
                    plan._items.push_back(item);
                }
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
            int64_t currentQty = 0;
            if (histExchange) {
                currentQty = histExchange->GetPositionQuantity(item._symbol);
            }
            if (currentQty < 0) {
                // 有空仓，BUY = 平空
                item._quantity = static_cast<int>(-currentQty);
                item._flag = 1; // 平仓
                item._limitPrice = price;
                item._targetValue = item._quantity * price;
                plan._items.push_back(item);
            } else if (currentQty > 0) {
                // 已有多仓，跳过
                INFO("Symbol {} already has position {}, skipping buy", symbolName, currentQty);
                continue;
            } else {
                // 无仓，BUY = 开多
                int64_t quantity = static_cast<int64_t>(perSymbolCapital / price / 100) * 100;
                if (quantity < 100) {
                    WARN("Quantity {} too small for symbol {}", quantity, symbolName);
                    continue;
                }
                item._quantity = quantity;
                item._flag = 0; // 开仓
                item._limitPrice = price;
                item._targetValue = quantity * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            }
        } else if (item._action == TradeAction::SELL) {
            int64_t currentQty = 0;
            if (histExchange) {
                currentQty = histExchange->GetPositionQuantity(item._symbol);
            }
            if (currentQty > 0) {
                // 平多
                item._quantity = currentQty;
                item._flag = 1; // 平仓
                item._limitPrice = price;
                item._targetValue = currentQty * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            } else if (currentQty == 0 && _allowShort) {
                // 做空（开仓）
                int64_t quantity = static_cast<int64_t>(perSymbolCapital / price / 100) * 100;
                if (quantity >= 100) {
                    item._quantity = quantity;
                    item._flag = 0; // 开仓
                    item._limitPrice = price;
                    item._targetValue = quantity * price;
                    plan._items.push_back(item);
                    plan._usedCapital += item._targetValue;
                }
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
            oldItem._quantity != newItem._quantity ||
            oldItem._flag != newItem._flag) {
            return true;
        }
    }

    return false;
}

const nlohmann::json PortfolioNode::getParams() {
    return {"positionRatio", "pool", "allowShort"};
}

Map<String, ArgType> PortfolioNode::out_elements() {
    Map<String, ArgType> elems;
    // 声明执行计划摘要数据，供 DebugNode 读取
    // 每个 symbol 有: .plan_action (1=BUY, -1=SELL, 0=HOLD), .plan_qty, .plan_price
    for (const auto& sym : _pool) {
        String s = get_symbol(sym);
        elems[s + ".plan_action"] = ArgType::Double_TimeSeries;
        elems[s + ".plan_qty"] = ArgType::Double_TimeSeries;
        elems[s + ".plan_price"] = ArgType::Double_TimeSeries;
    }
    return elems;
}
