#include "Nodes/ProtectionNode.h"
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

ProtectionNode::ProtectionNode(Server* server) : _server(server) {
}

ProtectionNode::~ProtectionNode() {
    if (_formulaParser) delete _formulaParser;
    // Phase 0: 内部计算器由 unique_ptr 自动清理
}

bool ProtectionNode::Init(const nlohmann::json& config) {
    auto& params = config["params"];

    // 支持三种格式：
    // 1. 前端中文扁平格式: "止损开关": { "value": true }, "止损比例": { "value": 0.05 }
    // 2. 后端导出英文格式: "stop_loss_enabled": { "value": true }, "stop_loss_percent": 0.05
    // 3. 嵌套格式: "stop_loss": { "enabled": true, "percent": 0.05 }

    String formulaExpr;

    if (params.contains("止损开关")) {
        // 前端中文格式
        _sl.enabled = params["止损开关"]["value"];
        _sl.percent = params["止损比例"]["value"];
        _tp.enabled = params["止盈开关"]["value"];
        _tp.percent = params["止盈比例"]["value"];
        _ts.enabled = params["追踪止损开关"]["value"];
        _ts.percent = params["追踪止损比例"]["value"];
        _time.enabled = params["时间止损开关"]["value"];
        _time.max_bars = params["最大持仓Bar数"]["value"];
        if (params.contains("公式止损开关")) {
            _formula.enabled = params["公式止损开关"]["value"];
        }
        if (params.contains("公式止损表达式")) {
            formulaExpr = params["公式止损表达式"]["value"].get<String>();
        }
        // Phase 0: 4 类新止损（中文格式）
        if (params.contains("ATR止损开关")) {
            _atr_sl.enabled = params["ATR止损开关"]["value"];
            _atr_sl.period = params.value("ATR周期", nlohmann::json::object({{"value", 20}}))["value"].get<int>();
            _atr_sl.multiplier = params.value("ATR倍数", nlohmann::json::object({{"value", 2.0}}))["value"].get<double>();
        }
        if (params.contains("MA止损开关")) {
            _ma_sl.enabled = params["MA止损开关"]["value"];
            _ma_sl.period = params.value("MA周期", nlohmann::json::object({{"value", 20}}))["value"].get<int>();
        }
        if (params.contains("R2止损开关")) {
            _r2_sl.enabled = params["R2止损开关"]["value"];
            _r2_sl.period = params.value("R2周期", nlohmann::json::object({{"value", 10}}))["value"].get<int>();
            _r2_sl.threshold = params.value("R2阈值", nlohmann::json::object({{"value", 0.5}}))["value"].get<double>();
        }
        if (params.contains("MAE止损开关")) {
            _mae_sl.enabled = params["MAE止损开关"]["value"];
            _mae_sl.percent = params.value("MAE比例", nlohmann::json::object({{"value", 0.05}}))["value"].get<double>();
        }
    } else if (params.contains("stop_loss_enabled")) {
        // 后端导出英文扁平格式
        _sl.enabled = params["stop_loss_enabled"]["value"];
        _sl.percent = params["stop_loss_percent"]["value"];
        _tp.enabled = params["take_profit_enabled"]["value"];
        _tp.percent = params["take_profit_percent"]["value"];
        _ts.enabled = params["trailing_stop_enabled"]["value"];
        _ts.percent = params["trailing_stop_percent"]["value"];
        _time.enabled = params["time_stop_enabled"]["value"];
        _time.max_bars = params["max_bars"]["value"];
        if (params.contains("formula_stop_enabled")) {
            _formula.enabled = params["formula_stop_enabled"]["value"];
        }
        if (params.contains("formula_stop_expression")) {
            formulaExpr = params["formula_stop_expression"]["value"].get<String>();
        }
        // Phase 0: 4 类新止损（英文扁平格式）
        if (params.contains("atr_stop_loss_enabled")) {
            _atr_sl.enabled = params["atr_stop_loss_enabled"]["value"];
            _atr_sl.period = params.value("atr_period", nlohmann::json::object({{"value", 20}}))["value"].get<int>();
            _atr_sl.multiplier = params.value("atr_multiplier", nlohmann::json::object({{"value", 2.0}}))["value"].get<double>();
        }
        if (params.contains("ma_stop_loss_enabled")) {
            _ma_sl.enabled = params["ma_stop_loss_enabled"]["value"];
            _ma_sl.period = params.value("ma_period", nlohmann::json::object({{"value", 20}}))["value"].get<int>();
        }
        if (params.contains("r2_stop_loss_enabled")) {
            _r2_sl.enabled = params["r2_stop_loss_enabled"]["value"];
            _r2_sl.period = params.value("r2_period", nlohmann::json::object({{"value", 10}}))["value"].get<int>();
            _r2_sl.threshold = params.value("r2_threshold", nlohmann::json::object({{"value", 0.5}}))["value"].get<double>();
        }
        if (params.contains("mae_stop_loss_enabled")) {
            _mae_sl.enabled = params["mae_stop_loss_enabled"]["value"];
            _mae_sl.percent = params.value("mae_percent", nlohmann::json::object({{"value", 0.05}}))["value"].get<double>();
        }
    } else if (params.contains("stop_loss")) {
        // 嵌套格式（直接 JSON 配置）
        _sl.enabled = params["stop_loss"]["enabled"];
        _sl.percent = params["stop_loss"]["percent"];
        _tp.enabled = params["take_profit"]["enabled"];
        _tp.percent = params["take_profit"]["percent"];
        _ts.enabled = params["trailing_stop"]["enabled"];
        _ts.percent = params["trailing_stop"]["percent"];
        _time.enabled = params["time_stop"]["enabled"];
        _time.max_bars = params["time_stop"]["max_bars"];
        if (params.contains("formula_stop")) {
            _formula.enabled = params["formula_stop"]["enabled"];
            formulaExpr = params["formula_stop"].value("expression", "");
        }
        // Phase 0: 4 类新止损（嵌套格式）
        if (params.contains("atr_stop_loss")) {
            _atr_sl.enabled = params["atr_stop_loss"].value("enabled", false);
            _atr_sl.period = params["atr_stop_loss"].value("period", 20);
            _atr_sl.multiplier = params["atr_stop_loss"].value("multiplier", 2.0);
        }
        if (params.contains("ma_stop_loss")) {
            _ma_sl.enabled = params["ma_stop_loss"].value("enabled", false);
            _ma_sl.period = params["ma_stop_loss"].value("period", 20);
        }
        if (params.contains("r2_stop_loss")) {
            _r2_sl.enabled = params["r2_stop_loss"].value("enabled", false);
            _r2_sl.period = params["r2_stop_loss"].value("period", 10);
            _r2_sl.threshold = params["r2_stop_loss"].value("threshold", 0.5);
        }
        if (params.contains("mae_stop_loss")) {
            _mae_sl.enabled = params["mae_stop_loss"].value("enabled", false);
            _mae_sl.percent = params["mae_stop_loss"].value("percent", 0.05);
        }
    }

    // 初始化公式止损
    if (_formula.enabled && !formulaExpr.empty()) {
        _formulaParser = new FormulaParser(_server);
        if (!_formulaParser->parse(formulaExpr)) {
            WARN("[ProtectionNode] failed to parse formula expression: {}", formulaExpr);
            delete _formulaParser;
            _formulaParser = nullptr;
            _formula.enabled = false;
            return false;
        }

        // 从上游输入收集 symbols 和 variant names
        for (auto& item : _ins) {
            auto outs = item.second->out_elements();
            for (auto& [key, type] : outs) {
                Vector<String> tokens;
                split(key, tokens, ".");
                if (tokens.size() >= 2) {
                    symbol_t sym = to_symbol(tokens[0] + "." + tokens[1]);
                    if (std::find(_formulaSymbols.begin(), _formulaSymbols.end(), sym) == _formulaSymbols.end()) {
                        _formulaSymbols.push_back(sym);
                    }
                }
                if (!tokens.empty()) {
                    _formulaVariants.insert(tokens.back());
                }
            }
        }

        INFO("[ProtectionNode] Formula stop enabled: expr='{}' symbols={} variants={}",
             formulaExpr, _formulaSymbols.size(), _formulaVariants.size());
    }

    // Phase 0: 日志输出新止损配置
    if (_atr_sl.enabled) {
        INFO("[ProtectionNode] ATR stop loss enabled: period={}, multiplier={}", _atr_sl.period, _atr_sl.multiplier);
    }
    if (_ma_sl.enabled) {
        INFO("[ProtectionNode] MA stop loss enabled: period={}", _ma_sl.period);
    }
    if (_r2_sl.enabled) {
        INFO("[ProtectionNode] R2 stop loss enabled: period={}, threshold={}", _r2_sl.period, _r2_sl.threshold);
    }
    if (_mae_sl.enabled) {
        INFO("[ProtectionNode] MAE stop loss enabled: percent={}", _mae_sl.percent);
    }

    return true;
}

static int64_t get_position_quantity(Server* server, DataContext& context, symbol_t symbol) {
    if (server->GetRunningMode() == RuningType::Backtest) {
        auto* histExchange = dynamic_cast<HistorySimulationBase*>(
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
        auto* histExchange = dynamic_cast<HistorySimulationBase*>(
            _server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
        if (histExchange) {
            auto run_id = context.getBacktestRunId();
            auto btCtx = histExchange->getBacktestContext(run_id);
            if (btCtx) {
                INFO("[ProtectionNode] syncPositions: run_id={}, epoch={}", run_id, context.GetEpoch());
                for (const auto& sym : btCtx->getSymbols()) {
                    int64_t pos = btCtx->getPosition(sym);
                    INFO("[ProtectionNode]   symbol={} position={}", get_symbol(sym), pos);
                    if (pos != 0) {
                        current_symbols.insert(sym);
                    }
                }
            } else {
                INFO("[ProtectionNode] syncPositions: btCtx is null for run_id={}", run_id);
            }
        } else {
            INFO("[ProtectionNode] syncPositions: histExchange is null");
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

            // 获取当前价作为初始最高价和最低价
            String close_key = get_symbol(symbol) + ".close";
            if (context.exist(close_key)) {
                auto price_var = context.get<Vector<double>>(close_key);
                if (!price_var.empty()) {
                    info.highest_price = price_var.back();
                    info.lowest_price = price_var.back();  // Phase 0: MAE 用
                }
            }

            _entry_info[symbol] = info;
            if (_server->GetRunningMode() != RuningType::Backtest) {
                STRATEGY_INFO(strategy, "[ProtectionNode] New position detected: {} cost={}", get_symbol(symbol), info.avg_price);
            } else {
                INFO("[ProtectionNode] New position detected: {} cost={}", get_symbol(symbol), info.avg_price);
            }
        }
    }
}

NodeProcessResult ProtectionNode::Process(const String& strategy, DataContext& context) {
    // 每轮开始时重置风控上下文
    auto* rc = context.GetRiskContext();
    rc->reset();

    // 从 Server 同步持仓
    syncPositions(strategy, context);

    INFO("[ProtectionNode] Process: epoch={}, _entry_info.size={}", context.GetEpoch(), _entry_info.size());

    if (_entry_info.empty()) {
        INFO("[ProtectionNode] _entry_info is empty, returning early");
        return NodeProcessResult::Success;
    }

    int current_bar = context.GetEpoch();
    RiskTriggerType triggered = RiskTriggerType::None;
    symbol_t triggered_symbol;
    double triggered_price = 0.0;

    // 公式止损：一次性计算所有标的
    Map<symbol_t, double> formulaResults;
    if (_formula.enabled && _formulaParser && !_formulaSymbols.empty()) {
        formulaResults = _formulaParser->computeNumeric(_formulaSymbols, _formulaVariants, context);
    }

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
            INFO("[ProtectionNode] StopLoss check: symbol={} avg_price={} current_price={} sl_price={} enabled={}", 
                 get_symbol(symbol), info.avg_price, current_price, sl_price, _sl.enabled);
            if (current_price <= sl_price) {
                INFO("[ProtectionNode] StopLoss TRIGGERED!");
                triggered = RiskTriggerType::StopLoss;
                triggered_symbol = symbol;
                triggered_price = current_price;
                break;
            }
        }

        // 2. 检查止盈: current > entry * (1 + percent)
        if (_tp.enabled && triggered == RiskTriggerType::None) {
            double tp_price = info.avg_price * (1.0 + _tp.percent);
            if (current_price >= tp_price) {
                triggered = RiskTriggerType::TakeProfit;
                triggered_symbol = symbol;
                triggered_price = current_price;
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
                triggered_price = current_price;
                break;
            }
        }

        // 4. 检查时间止损: held_bars > max_bars
        if (_time.enabled && triggered == RiskTriggerType::None) {
            int held_bars = current_bar - info.entry_bar;
            if (held_bars >= _time.max_bars) {
                triggered = RiskTriggerType::TimeStop;
                triggered_symbol = symbol;
                triggered_price = current_price;
                break;
            }
        }

        // 5. 检查公式止损: 表达式结果非零即触发
        if (_formula.enabled && triggered == RiskTriggerType::None) {
            auto it = formulaResults.find(symbol);
            if (it != formulaResults.end() && it->second != 0.0) {
                triggered = RiskTriggerType::FormulaStop;
                triggered_symbol = symbol;
                triggered_price = current_price;
                break;
            }
        }

        // ===== Phase 0: 4 类新止损 =====

        // 6. ATR 自适应止损: current < entry - multiplier * ATR(period)
        if (_atr_sl.enabled && triggered == RiskTriggerType::None) {
            // 懒初始化计算器
            if (!_atr_calc[symbol]) {
                _atr_calc[symbol] = std::make_unique<ATR>(_atr_sl.period);
            }
            // 构造输入参数
            Map<String, context_t> args;
            String prefix = get_symbol(symbol) + ".";
            if (context.exist(prefix + "high"))  args["high"]  = context.get<Vector<double>>(prefix + "high");
            if (context.exist(prefix + "low"))   args["low"]   = context.get<Vector<double>>(prefix + "low");
            if (context.exist(prefix + "close")) args["close"] = context.get<Vector<double>>(prefix + "close");
            
            context_t result = (*_atr_calc[symbol])(args);
            double atr = std::visit([](const auto& v) -> double {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, double>) return v;
                else if constexpr (std::is_same_v<T, Vector<double>>) 
                    return v.empty() ? std::nan("") : v.back();
                else return std::nan("");
            }, result);
            
            if (!std::isnan(atr) && atr > 0) {
                double sl_price = info.avg_price - _atr_sl.multiplier * atr;
                if (current_price <= sl_price) {
                    triggered = RiskTriggerType::AtrStopLoss;
                    triggered_symbol = symbol;
                    triggered_price = current_price;
                    break;
                }
            }
        }

        // 7. MA 跌破止损: current <= MA(period)
        if (_ma_sl.enabled && triggered == RiskTriggerType::None) {
            if (!_ma_calc[symbol]) {
                _ma_calc[symbol] = std::make_unique<MA>(_ma_sl.period);
            }
            Map<String, context_t> args;
            String close_key = get_symbol(symbol) + ".close";
            if (context.exist(close_key)) {
                args["close"] = context.get<Vector<double>>(close_key);
            }
            
            context_t result = (*_ma_calc[symbol])(args);
            double ma = std::visit([](const auto& v) -> double {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, double>) return v;
                else if constexpr (std::is_same_v<T, Vector<double>>) 
                    return v.empty() ? std::nan("") : v.back();
                else return std::nan("");
            }, result);
            
            if (!std::isnan(ma) && current_price <= ma) {
                triggered = RiskTriggerType::MaStopLoss;
                triggered_symbol = symbol;
                triggered_price = current_price;
                break;
            }
        }

        // 8. R² 趋势消失止损: R2(period) <= threshold
        if (_r2_sl.enabled && triggered == RiskTriggerType::None) {
            if (!_r2_calc[symbol]) {
                _r2_calc[symbol] = std::make_unique<R2>(_r2_sl.period);
            }
            Map<String, context_t> args;
            String close_key = get_symbol(symbol) + ".close";
            if (context.exist(close_key)) {
                args["close"] = context.get<Vector<double>>(close_key);
            }
            
            context_t result = (*_r2_calc[symbol])(args);
            double r2 = std::visit([](const auto& v) -> double {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, double>) return v;
                else if constexpr (std::is_same_v<T, Vector<double>>) 
                    return v.empty() ? std::nan("") : v.back();
                else return std::nan("");
            }, result);
            
            if (!std::isnan(r2) && r2 <= _r2_sl.threshold) {
                triggered = RiskTriggerType::R2StopLoss;
                triggered_symbol = symbol;
                triggered_price = current_price;
                break;
            }
        }

        // 9. MAE 最大不利偏移止损: (entry - lowest) / entry >= percent
        if (_mae_sl.enabled && triggered == RiskTriggerType::None) {
            double lowest = info.lowest_price > 0 ? info.lowest_price : info.avg_price;
            double mae = (info.avg_price - lowest) / info.avg_price;
            if (mae >= _mae_sl.percent) {
                triggered = RiskTriggerType::MaeStopLoss;
                triggered_symbol = symbol;
                triggered_price = current_price;
                break;
            }
        }

        // 更新最高价和最低价
        auto& entry = _entry_info[symbol];
        if (current_price > entry.highest_price) {
            entry.highest_price = current_price;
        }
        if (current_price < entry.lowest_price || entry.lowest_price == 0) {
            entry.lowest_price = current_price;
        }
    }

    if (triggered != RiskTriggerType::None) {
        rc->triggered = true;
        rc->trigger_type = triggered;
        rc->action = RiskAction::Close;

        ProtectionEvent evt;
        evt.bar_index = current_bar;
        evt.datetime = context.Current();
        evt.symbol = triggered_symbol;
        evt.type = triggered;
        evt.entry_price = _entry_info[triggered_symbol].avg_price;
        evt.current_price = triggered_price;
        _events.push_back(evt);

        if (_server->GetRunningMode() != RuningType::Backtest) {
            STRATEGY_INFO(strategy, "[ProtectionNode] Risk triggered: symbol={} type={} cost={} current={}",
                 get_symbol(triggered_symbol), to_string(triggered),
                 evt.entry_price, evt.current_price);
        } else {
            INFO("[ProtectionNode] Risk triggered: symbol={} type={} cost={} current={}",
                 get_symbol(triggered_symbol), to_string(triggered),
                 evt.entry_price, evt.current_price);
        }
    }

    return NodeProcessResult::Success;
}

const nlohmann::json ProtectionNode::getParams() {
    return {"stop_loss", "take_profit", "trailing_stop", "time_stop", "formula_stop",
            "atr_stop_loss", "ma_stop_loss", "r2_stop_loss", "mae_stop_loss"};
}

Map<String, ArgType> ProtectionNode::out_elements() {
    return {};
}
