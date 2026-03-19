#include "Nodes/PortfolioNode.h"
#include "server.h"
#include "Bridge/exchange.h"
#include "Util/log.h"
#include "PortfolioSubsystem.h"

PortfolioNode::PortfolioNode(Server* server)
    : _server(server)
    , _positionRatio(0.5)
    , _initialCapital(1000000.0)
{
}

PortfolioNode::~PortfolioNode() = default;

bool PortfolioNode::Init(const nlohmann::json& config) {
    // 仓位比例
    if (config["params"].contains("positionRatio")) {
        _positionRatio = config["params"]["positionRatio"]["value"];
    }

    // 交易池
    if (config["params"].contains("pool")) {
        auto& poolConfig = config["params"]["pool"]["value"];
        for (const String& code : poolConfig) {
            auto& security = Server::GetSecurity(code);
            _pool.emplace_back(to_symbol(code, security));
        }
    }

    // 初始本金 (回测模式专用)
    if (config["params"].contains("initialCapital")) {
        _initialCapital = config["params"]["initialCapital"]["value"];
    }

    return true;
}

bool PortfolioNode::Process(const String& strategy, DataContext& context) {
    // 1. 获取上游节点的信号
    Vector<symbol_t> symbols;
    Vector<TradeAction> actions;

    for (const auto& symbol : _pool) {
        String signalKey = get_symbol(symbol) + ".signal";
        if (context.exist(signalKey)) {
            const auto& signalVal = context.get(signalKey);
            double signal = std::get<double>(signalVal);

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
    double capital = getAvailableCapital();
    double targetCapital = capital * _positionRatio;

    // 3. 生成执行计划
    ExecutionPlan newPlan = generatePlan(symbols, actions, targetCapital);

    // 4. 检查是否有变化
    newPlan._hasChanged = isPlanChanged(newPlan);

    // 5. 如果有变化或首次执行，输出执行计划
    if (newPlan._hasChanged || _lastPlan._items.empty()) {
        _lastPlan._items.clear();
        _lastPlan._items = std::move(newPlan._items);
        _lastPlan._totalCapital = newPlan._totalCapital;
        _lastPlan._usedCapital = newPlan._usedCapital;
        _lastPlan._hasChanged = true;

        context.GetExecutionPlan() = _lastPlan;
        INFO("PortfolioNode: generated new execution plan with {} items, capital={}",
             _lastPlan._items.size(), capital);
    } else {
        context.GetExecutionPlan()._hasChanged = false;
        INFO("PortfolioNode: no change, keeping current positions");
    }

    return true;
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

        auto quote = exchange->GetQuote(item._symbol);
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

double PortfolioNode::getAvailableCapital() {
    auto runMode = _server->GetRunningMode();

    // 回测模式: 使用配置的初始本金
    if (runMode == RuningType::Backtest) {
        INFO("PortfolioNode: Backtest mode, using initial capital = {}", _initialCapital);
        return _initialCapital;
    }

    // 仿真模式或实盘模式: 使用交易所返回的可用资金
    auto* exchange = _server->GetAvaliableStockExchange();
    if (exchange) {
        double funds = exchange->GetAvailableFunds();
        if (funds > 0) {
            //INFO("PortfolioNode: {}/{} mode, using exchange funds = {}",
            //     runMode == RuningType::Simualtion ? "Simulation" : "Real", funds);
            return funds;
        }
    }

    // 备选: 从PortfolioSubSystem获取本金
    auto* portfolio = _server->GetPortforlioSubSystem();
    if (portfolio && !portfolio->GetAllPortfolio().empty()) {
        double principal = portfolio->GetPortfolio()._principal;
        if (principal > 0) {
            INFO("PortfolioNode: using portfolio principal = {}", principal);
            return principal;
        }
    }

    // 最终备选: 使用配置的初始本金
    WARN("PortfolioNode: exchange funds unavailable, fallback to initial capital = {}", _initialCapital);
    return _initialCapital;
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
    elems["execution_plan"] = ArgType::Integer;
    return elems;
}
