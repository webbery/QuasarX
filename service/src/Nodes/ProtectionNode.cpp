#include "Nodes/ProtectionNode.h"
#include "DataContext.h"
#include "RiskContext.h"
#include "BrokerSubSystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Util/log.h"
#include "server.h"

ProtectionNode::ProtectionNode(Server* server) : _server(server) {
}

ProtectionNode::~ProtectionNode() {
}

bool ProtectionNode::Init(const nlohmann::json& config) {
    auto& params = config["params"];

    // 支持两种格式：
    // 1. 嵌套格式（直接 JSON 配置）: "stop_loss": { "enabled": true, "percent": 0.05 }
    // 2. 扁平格式（前端导出）: "止损开关": { "value": true }, "止损比例": { "value": 0.05 }

    // 扁平格式（前端参数名 → 后端配置）
    if (params.contains("止损开关") && params.contains("止损比例")) {
        _sl.enabled = params["止损开关"]["value"];
        _sl.percent = params["止损比例"]["value"];
        _tp.enabled = params["止盈开关"]["value"];
        _tp.percent = params["止盈比例"]["value"];
        _ts.enabled = params["追踪止损开关"]["value"];
        _ts.percent = params["追踪止损比例"]["value"];
        _time.enabled = params["时间止损开关"]["value"];
        _time.max_bars = params["最大持仓Bar数"]["value"];
    } else if (params.contains("stop_loss")) {
        // 嵌套格式
        _sl.enabled = params["stop_loss"]["enabled"];
        _sl.percent = params["stop_loss"]["percent"];
        _tp.enabled = params["take_profit"]["enabled"];
        _tp.percent = params["take_profit"]["percent"];
        _ts.enabled = params["trailing_stop"]["enabled"];
        _ts.percent = params["trailing_stop"]["percent"];
        _time.enabled = params["time_stop"]["enabled"];
        _time.max_bars = params["time_stop"]["max_bars"];
    }

    return true;
}

static int64_t get_position_quantity(Server* server, DataContext& context, symbol_t symbol) {
    if (server->GetRunningMode() == RuningType::Backtest) {
        auto* histExchange = dynamic_cast<StockHistorySimulation*>(
            server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
        if (histExchange) {
            return histExchange->GetPositionQuantity(symbol);
        }
    } else {
        auto& ap = server->GetPosition("");
        for (const auto& pos : ap._positions) {
            if (pos._symbol == symbol) {
                return pos._holds;
            }
        }
    }
    return 0;
}

static double get_position_cost(Server* server, DataContext& context, symbol_t symbol) {
    if (server->GetRunningMode() == RuningType::Backtest) {
        // 回测模式下 BacktestContext 不保存成本价，返回 0
        // ProtectionNode 在新建仓时自己记录当前价作为成本
        return 0.0;
    } else {
        auto& ap = server->GetPosition("");
        for (const auto& pos : ap._positions) {
            if (pos._symbol == symbol) {
                return pos._price;
            }
        }
    }
    return 0.0;
}

void ProtectionNode::syncPositions(const String& strategy, DataContext& context) {
    // 获取当前所有有持仓的标的
    Set<symbol_t> current_symbols;
    if (_server->GetRunningMode() == RuningType::Backtest) {
        auto* histExchange = dynamic_cast<StockHistorySimulation*>(
            _server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
        if (histExchange) {
            auto run_id = context.getBacktestRunId();
            auto btCtx = histExchange->getBacktestContext(run_id);
            if (btCtx) {
                for (const auto& sym : btCtx->getSymbols()) {
                    if (btCtx->getPosition(sym) != 0) {
                        current_symbols.insert(sym);
                    }
                }
            }
        }
    } else {
        auto& ap = _server->GetPosition("");
        for (const auto& pos : ap._positions) {
            if (pos._holds != 0) {
                current_symbols.insert(pos._symbol);
            }
        }
    }

    // 移除已平仓的标的
    for (auto it = _entry_info.begin(); it != _entry_info.end(); ) {
        if (!current_symbols.count(it->first)) {
            it = _entry_info.erase(it);
        } else {
            ++it;
        }
    }

    // 新增持仓：记录入场信息
    for (const auto& symbol : current_symbols) {
        if (!_entry_info.count(symbol)) {
            EntryInfo info;
            info.entry_bar = context.GetEpoch();

            // 获取成本价（实盘模式从 position 读取，回测模式为 0）
            double cost = get_position_cost(_server, context, symbol);
            if (cost > 0) {
                info.avg_price = cost;
            } else {
                // 回测模式或无成本价：用当前 Bar 收盘价作为近似入场价
                String close_key = get_symbol(symbol) + ".close";
                if (context.exist(close_key)) {
                    auto price_var = context.get<Vector<double>>(close_key);
                    if (!price_var.empty()) {
                        info.avg_price = price_var.back();
                    }
                }
            }

            // 获取当前价作为初始最高价
            String close_key = get_symbol(symbol) + ".close";
            if (context.exist(close_key)) {
                auto price_var = context.get<Vector<double>>(close_key);
                if (!price_var.empty()) {
                    info.highest_price = price_var.back();
                }
            }

            _entry_info[symbol] = info;
            INFO("[ProtectionNode] New position detected: {} cost={}", get_symbol(symbol), info.avg_price);
        }
    }
}

NodeProcessResult ProtectionNode::Process(const String& strategy, DataContext& context) {
    // 每轮开始时重置风控上下文
    auto* rc = context.GetRiskContext();
    rc->reset();

    // 从 Server 同步持仓
    syncPositions(strategy, context);

    if (_entry_info.empty()) {
        return NodeProcessResult::Success;
    }

    int current_bar = context.GetEpoch();
    RiskTriggerType triggered = RiskTriggerType::None;
    symbol_t triggered_symbol;

    for (const auto& [symbol, info] : _entry_info) {
        if (info.avg_price <= 0) continue;

        // 获取当前价
        double current_price = 0.0;
        String close_key = get_symbol(symbol) + ".close";
        if (context.exist(close_key)) {
            auto price_var = context.get<Vector<double>>(close_key);
            if (!price_var.empty()) {
                current_price = price_var.back();
            }
        }
        if (current_price <= 0) continue;

        // 1. 检查止损: current < entry * (1 - percent)
        if (_sl.enabled) {
            double sl_price = info.avg_price * (1.0 - _sl.percent);
            if (current_price <= sl_price) {
                triggered = RiskTriggerType::StopLoss;
                triggered_symbol = symbol;
                break;
            }
        }

        // 2. 检查止盈: current > entry * (1 + percent)
        if (_tp.enabled && triggered == RiskTriggerType::None) {
            double tp_price = info.avg_price * (1.0 + _tp.percent);
            if (current_price >= tp_price) {
                triggered = RiskTriggerType::TakeProfit;
                triggered_symbol = symbol;
                break;
            }
        }

        // 3. 检查追踪止损: current < highest * (1 - percent)
        if (_ts.enabled && triggered == RiskTriggerType::None) {
            double highest = info.highest_price > 0 ? info.highest_price : info.avg_price;
            double ts_price = highest * (1.0 - _ts.percent);
            if (current_price <= ts_price) {
                triggered = RiskTriggerType::TrailingStop;
                triggered_symbol = symbol;
                break;
            }
        }

        // 4. 检查时间止损: held_bars > max_bars
        if (_time.enabled && triggered == RiskTriggerType::None) {
            int held_bars = current_bar - info.entry_bar;
            if (held_bars >= _time.max_bars) {
                triggered = RiskTriggerType::TimeStop;
                triggered_symbol = symbol;
                break;
            }
        }

        // 更新最高价
        auto& entry = _entry_info[symbol];
        if (current_price > entry.highest_price) {
            entry.highest_price = current_price;
        }
    }

    if (triggered != RiskTriggerType::None) {
        rc->triggered = true;
        rc->trigger_type = triggered;
        rc->action = RiskAction::Close;
        INFO("[ProtectionNode] Risk triggered: symbol={} type={} cost={} current={}",
             get_symbol(triggered_symbol), to_string(triggered),
             _entry_info[triggered_symbol].avg_price,
             context.get<Vector<double>>(get_symbol(triggered_symbol) + ".close").back());
    }

    return NodeProcessResult::Success;
}

const nlohmann::json ProtectionNode::getParams() {
    return {"stop_loss", "take_profit", "trailing_stop", "time_stop"};
}

Map<String, ArgType> ProtectionNode::out_elements() {
    return {};
}
