#include "Interprecter/Stmt.h"
#include "DataContext.h"
#include "BrokerSubSystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Function/Function.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "server.h"
#include "Nodes/ProtectionNode.h"

ProtectionNode::ProtectionNode(Server* server) : _server(server) {
}

ProtectionNode::~ProtectionNode() {
    if (_formulaParser) delete _formulaParser;
    // _guards 由 unique_ptr 自动清理
}

bool ProtectionNode::Init(const nlohmann::json& config) {
    auto& params = config["params"];

    // 使用 GuardFactory 创建 Guard 实例
    _guards = GuardFactory::createGuards(params);

    // 日志输出创建的 Guard
    INFO("[ProtectionNode] Created {} guards:", _guards.size());
    for (const auto& guard : _guards) {
        INFO("[ProtectionNode]   - {} (enabled={})", guard->name(), guard->enabled());
    }

    // 公式止损需要特殊处理（暂不纳入 Guard 架构）
    String formulaExpr;
    if (params.contains("公式止损开关") && params["公式止损开关"]["value"].get<bool>()) {
        _formulaEnabled = true;
        formulaExpr = params.value("公式止损表达式", nlohmann::json::object())["value"].get<String>();
    } else if (params.contains("formula_stop_enabled") && params["formula_stop_enabled"]["value"].get<bool>()) {
        _formulaEnabled = true;
        formulaExpr = params.value("formula_stop_expression", nlohmann::json::object())["value"].get<String>();
    } else if (params.contains("formula_stop") && params["formula_stop"]["enabled"].get<bool>()) {
        _formulaEnabled = true;
        formulaExpr = params["formula_stop"].value("expression", "");
    }

    if (_formulaEnabled && !formulaExpr.empty()) {
        _formulaExpr = formulaExpr;
        _formulaParser = new FormulaParser(_server);
        if (!_formulaParser->parse(formulaExpr)) {
            WARN("[ProtectionNode] failed to parse formula expression: {}", formulaExpr);
            delete _formulaParser;
            _formulaParser = nullptr;
            _formulaEnabled = false;
            return false;
        }
        INFO("[ProtectionNode] Formula stop enabled: expr='{}'", formulaExpr);
    }

    return true;
}

const nlohmann::json ProtectionNode::getParams() {
    return {
        {"止损", {
            {"止损开关", {{"type", "checkbox"}, {"value", false}}},
            {"止损比例", {{"type", "number"}, {"value", 0.08}, {"step", 0.01}, {"min", 0.01}, {"max", 0.5}}}
        }},
        {"止盈", {
            {"止盈开关", {{"type", "checkbox"}, {"value", false}}},
            {"止盈比例", {{"type", "number"}, {"value", 0.05}, {"step", 0.01}, {"min", 0.01}, {"max", 0.5}}}
        }},
        {"追踪止损", {
            {"追踪止损开关", {{"type", "checkbox"}, {"value", false}}},
            {"追踪止损比例", {{"type", "number"}, {"value", 0.03}, {"step", 0.01}, {"min", 0.01}, {"max", 0.3}}}
        }},
        {"时间止损", {
            {"时间止损开关", {{"type", "checkbox"}, {"value", false}}},
            {"最大持仓Bar数", {{"type", "number"}, {"value", 10}, {"min", 1}, {"max", 100}}}
        }},
        {"ATR止损", {
            {"ATR止损开关", {{"type", "checkbox"}, {"value", false}}},
            {"ATR周期", {{"type", "number"}, {"value", 20}, {"min", 5}, {"max", 60}}},
            {"ATR倍数", {{"type", "number"}, {"value", 2.0}, {"step", 0.5}, {"min", 1.0}, {"max", 5.0}}}
        }},
        {"MA止损", {
            {"MA止损开关", {{"type", "checkbox"}, {"value", false}}},
            {"MA周期", {{"type", "number"}, {"value", 20}, {"min", 5}, {"max", 60}}}
        }},
        {"R2止损", {
            {"R2止损开关", {{"type", "checkbox"}, {"value", false}}},
            {"R2周期", {{"type", "number"}, {"value", 10}, {"min", 5}, {"max", 30}}},
            {"R2阈值", {{"type", "number"}, {"value", 0.5}, {"step", 0.1}, {"min", 0.0}, {"max", 1.0}}}
        }},
        {"MAE止损", {
            {"MAE止损开关", {{"type", "checkbox"}, {"value", false}}},
            {"MAE比例", {{"type", "number"}, {"value", 0.05}, {"step", 0.01}, {"min", 0.01}, {"max", 0.3}}}
        }}
    };
}

Map<String, ArgType> ProtectionNode::out_elements() {
    return {};
}

static double get_position_cost_impl(Server* server, DataContext& context, const String& strategy, symbol_t symbol) {
    if (server->GetRunningMode() == RuningType::Backtest) {
        return 0.0;  // 回测模式由 ProtectionNode 自己记录
    } else {
        auto* broker = server->GetBrokerSubSystem();
        auto* capital = broker->GetStrategyCapital(strategy);
        auto pos = capital->getPosition(symbol);
        return pos._avgPrice;
    }
}

static int64_t get_position_qty_impl(Server* server, DataContext& context, const String& strategy, symbol_t symbol) {
    if (server->GetRunningMode() == RuningType::Backtest) {
        auto* histExchange = dynamic_cast<HistorySimulationBase*>(
            server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
        if (histExchange) {
            auto run_id = context.getBacktestRunId();
            auto btCtx = histExchange->getBacktestContext(run_id);
            if (btCtx) return btCtx->getPosition(symbol);
        }
        return 0;
    }
    auto* broker = server->GetBrokerSubSystem();
    auto* capital = broker->GetStrategyCapital(strategy);
    return capital->getHoldingQuantity(symbol);
}

double ProtectionNode::get_position_cost(const String& strategy, DataContext& context, symbol_t symbol) {
    return get_position_cost_impl(_server, context, strategy, symbol);
}

int64_t ProtectionNode::get_position_qty(const String& strategy, DataContext& context, symbol_t symbol) {
    return get_position_qty_impl(_server, context, strategy, symbol);
}

void ProtectionNode::syncPositions(const String& strategy, DataContext& context) {
    // 获取当前所有有持仓的标的（同时记录数量符号：负 = 空头）
    Set<symbol_t> current_symbols;
    Map<symbol_t, int64_t> current_qty;
    if (_server->GetRunningMode() == RuningType::Backtest) {
        auto* histExchange = dynamic_cast<HistorySimulationBase*>(
            _server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
        if (histExchange) {
            auto run_id = context.getBacktestRunId();
            auto btCtx = histExchange->getBacktestContext(run_id);
            if (btCtx) {
                for (const auto& sym : btCtx->getSymbols()) {
                    int64_t pos = btCtx->getPosition(sym);
                    if (pos != 0) {
                        current_symbols.insert(sym);
                        current_qty[sym] = pos;
                    }
                }
            }
        }
    } else {
        auto* broker = _server->GetBrokerSubSystem();
        auto* capital = broker->GetStrategyCapital(strategy);
        const time_t now = context.Current();
        for (const auto& [symbol, pos] : capital->getPositions().getAll()) {
            if (pos._quantity == 0) continue;
            const time_t entry_time = capital->getPositionEntryTime(symbol);
            if (entry_time > 0 && now > 0 && now < entry_time) {
                // 静默跳过会让"持仓明明在、风控却没触发"完全无从排查。
                // 只在决策 bar 记一条：日线回放时这段在每个历史 epoch 都成立，
                // 全记会把日志刷成 160 行/标的。
                if (context.IsDecisionBar()) {
                    STRATEGY_INFO(strategy,
                                  "[ProtectionNode] position skipped: {} entry_time={} > bar_time={}",
                                  get_symbol(symbol), entry_time, now);
                }
                continue;
            }
            current_symbols.insert(symbol);
            current_qty[symbol] = pos._quantity;
        }
    }

    // 移除已平仓的标的
    for (auto it = _entry_info.begin(); it != _entry_info.end(); ) {
        if (!current_symbols.count(it->first)) {
            _pending_close.erase(it->first);
            it = _entry_info.erase(it);
        } else {
            ++it;
        }
    }

    // 新增持仓：记录入场信息
    for (const auto& symbol : current_symbols) {
        auto qit = current_qty.find(symbol);
        const bool isShort = (qit != current_qty.end() && qit->second < 0);

        auto infoIt = _entry_info.find(symbol);
        if (infoIt != _entry_info.end() && infoIt->second._isShort != isShort) {
            // 多空翻转：旧方向的成本价/极值全部失效，按新仓重新初始化
            _entry_info.erase(infoIt);
        }

        if (!_entry_info.count(symbol)) {
            EntryInfo info;
            info._entry_bar = context.GetEpoch();
            info._isShort = isShort;

            double factor = context.GetAdjFactor(symbol);
            double cost = get_position_cost(strategy, context, symbol);
            if (cost > 0) {
                info._raw_cost = cost;
                info._avg_price = cost * factor;
            } else {
                // 回测模式或无成本价：用当前收盘价作为入场价
                String close_key = get_symbol(symbol) + ".close";
                if (context.exist(close_key)) {
                    auto price_var = context.get<Vector<double>>(close_key);
                    if (!price_var.empty()) {
                        info._avg_price = price_var.back();
                    }
                }
                if (info._avg_price > 0 && factor > 0) {
                    info._raw_cost = info._avg_price / factor;
                }
            }

            // 初始化最高/最低价
            double high = info._avg_price > 0 ? info._avg_price : 0.0;
            double low = high;
            String close_key = get_symbol(symbol) + ".close";
            if (context.exist(close_key)) {
                auto price_var = context.get<Vector<double>>(close_key);
                if (!price_var.empty()) {
                    const double close = price_var.back();
                    high = (high > 0) ? std::max(high, close) : close;
                    low = (low > 0) ? std::min(low, close) : close;
                }
            }
            info._highest_price = high;
            info._lowest_price = low;

            _entry_info[symbol] = info;
            if (_server->GetRunningMode() != RuningType::Backtest) {
                STRATEGY_INFO(strategy, "[ProtectionNode] New position detected: {} {} cost={}",
                             get_symbol(symbol), isShort ? "SHORT" : "LONG", info._raw_cost);
            }
        }
    }
}

RiskTriggerType ProtectionNode::checkAllGuards(symbol_t symbol, DataContext& context, const EntryInfo& info) {
    for (const auto& guard : _guards) {
        if (guard->enabled()) {
            RiskTriggerType hit = guard->check(symbol, context, info);
            if (hit != RiskTriggerType::None) {
                return hit;
            }
        }
    }
    return RiskTriggerType::None;
}

void ProtectionNode::updateAllGuards(symbol_t symbol, DataContext& context, EntryInfo& info) {
    for (const auto& guard : _guards) {
        if (guard->enabled()) {
            guard->update(symbol, context, info);
        }
    }
}

void ProtectionNode::handleTrigger(symbol_t symbol, RiskTriggerType type, const String& strategy,
                                   DataContext& context, const EntryInfo& info) {
    _pending_close.insert(symbol);

    ProtectionEvent evt;
    evt._bar_index = context.GetEpoch();
    evt._datetime = context.Current();
    evt._symbol = symbol;
    evt._type = type;
    evt._entry_price = info._raw_cost > 0 ? info._raw_cost : info._avg_price;
    evt._current_price = context.GetRawPrice(symbol);
    evt._quantity = get_position_qty(strategy, context, symbol);
    _events.push_back(evt);

    if (_server->GetRunningMode() != RuningType::Backtest) {
        STRATEGY_INFO(strategy, "[ProtectionNode] Risk triggered: symbol={} type={} cost={} current={} qty={}",
                     get_symbol(symbol), to_string(type),
                     evt._entry_price, evt._current_price, evt._quantity);
    } else {
        INFO("[ProtectionNode] Risk triggered: symbol={} type={} cost={} current={} qty={}",
             get_symbol(symbol), to_string(type),
             evt._entry_price, evt._current_price, evt._quantity);
    }
}

NodeProcessResult ProtectionNode::Process(const String& strategy, DataContext& context) {
    // 重置风控上下文
    auto* rc = context.GetRiskContext();
    rc->reset();

    // 从 Server 同步持仓
    syncPositions(strategy, context);

    if (_entry_info.empty()) {
        return NodeProcessResult::Success;
    }

    // 实盘策略：非决策 bar 只更新内部状态，不触发止损
    bool isRealtimeStrategy = (_server->GetRunningMode() != RuningType::Backtest);
    bool isDecisionBar = context.IsDecisionBar();

    if (isRealtimeStrategy && !isDecisionBar) {
        // 非决策 bar：只更新 Guard 状态
        for (auto& [symbol, info] : _entry_info) {
            updateAllGuards(symbol, context, info);
        }
        return NodeProcessResult::Success;
    }

    // 回测模式 或 决策 bar：检查止损
    RiskTriggerType triggered = RiskTriggerType::None;
    Set<symbol_t> triggered_symbols;

    for (auto& [symbol, info] : _entry_info) {
        if (info._avg_price <= 0) continue;
        if (_pending_close.count(symbol)) continue;

        RiskTriggerType hit = checkAllGuards(symbol, context, info);

        if (hit != RiskTriggerType::None) {
            if (triggered == RiskTriggerType::None) {
                triggered = hit;
            }
            triggered_symbols.insert(symbol);
        }

        // 更新状态
        updateAllGuards(symbol, context, info);
    }

    // 写入风控上下文
    if (!triggered_symbols.empty()) {
        rc->_trigger_type = triggered;
        rc->_action = RiskAction::Close;
        // 触发标的必须写进 RiskContext：Triggered() 的判据就是这个集合，
        // 只写 type/action 的话 ExecuteNode 永远不会走紧急平仓分支。
        rc->_trigger_symbols = triggered_symbols;
        for (symbol_t sym : triggered_symbols) {
            handleTrigger(sym, triggered, strategy, context, _entry_info[sym]);
        }
    }

    return NodeProcessResult::Success;
}

void ProtectionNode::postExecuteCheck(const String& strategy, DataContext& context) {
    // 在 ExecuteNode 执行后检查新买入的标的
    // 先同步持仓（包括今天新买入的）
    syncPositions(strategy, context);

    if (_entry_info.empty()) {
        return;
    }

    // 检查所有持仓
    RiskTriggerType triggered = RiskTriggerType::None;
    Set<symbol_t> triggered_symbols;

    for (auto& [symbol, info] : _entry_info) {
        if (info._avg_price <= 0) continue;
        if (_pending_close.count(symbol)) continue;

        RiskTriggerType hit = checkAllGuards(symbol, context, info);

        if (hit != RiskTriggerType::None) {
            if (triggered == RiskTriggerType::None) {
                triggered = hit;
            }
            triggered_symbols.insert(symbol);
        }
    }

    // 如果有触发，写入风控上下文
    if (!triggered_symbols.empty()) {
        auto* rc = context.GetRiskContext();
        rc->_trigger_type = triggered;
        rc->_action = RiskAction::Close;
        // 同 Process：Triggered() 判据是 _trigger_symbols，缺了它下游看不到风控
        rc->_trigger_symbols = triggered_symbols;

        for (symbol_t sym : triggered_symbols) {
            handleTrigger(sym, triggered, strategy, context, _entry_info[sym]);
        }

        INFO("[ProtectionNode] postExecuteCheck triggered {} symbols", triggered_symbols.size());
    }
}