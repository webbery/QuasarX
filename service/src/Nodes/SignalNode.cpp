#include "Nodes/SignalNode.h"
#include "DataContext.h"
#include "ExchangeManager.h"
#include "Interprecter/Stmt.h"
#include "Util/log.h"
#include "Util/system.h"
#include "Util/FinanceDB.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Nodes/QuoteNode.h"
#include <utility>
#include "boost/algorithm/string.hpp"

SignalNode::SignalNode(Server* server):SignalNodeBase(server), _buyParser(nullptr), _sellParser(nullptr) {
}

bool SignalNode::Init(const nlohmann::json& config) {
    auto& buySignal = config["params"]["buy"]["value"];
    auto& sellSignal = config["params"]["sell"]["value"];

    // 保存原始表达式（用于日志）
    _buyExpression = (String)buySignal;
    _sellExpression = (String)sellSignal;

    // 收集可用变量列表及其类型（从输入节点）
    Map<String, ArgType> availableVars;
    for (auto& item: _ins) {
        auto names = item.second->out_elements();
        for (auto& info: names) {
            // info.first 是完整 key 如 "bj.920108.ma_short"
            // info.second 是 ArgType
            availableVars[info.first] = info.second;
        }
    }

    // 基类初始化：allowShort + 上游标的池（提前到 validate() 之前，
    // 这样 validate() 才能按 short-name ("close") 正确解析 availableVars，
    // 不再因为 key 名不匹配而把类型校验静默放行）+ 可做空名单
    if (!SignalNodeBase::Init(config)) {
        return false;
    }

    // 解析/验证阶段打点：validate() 成功时是静默的，Init 中途抛异常时
    // 只剩一句 e.what()（如 "map::at"），无法判断断在哪个阶段。
    DEBUG_INFO("[SignalNode:{}] stage: parse buy", _id);

    // 解析并验证买入公式
    if (!_buyParser) {
        _buyParser = new FormulaParser(_server);
    }
    if (!_buyParser->parse(buySignal, TradeAction::BUY)) {
        WARN("parse buy express fail.");
        delete _buyParser;
        _buyParser = nullptr;
        throw std::runtime_error("Failed to parse buy signal expression");
    }
    DEBUG_INFO("[SignalNode:{}] stage: validate buy", _id);
    // 新增：类型验证 (带 symbols 以便 short-name 正确解析)
    if (!_buyParser->validate(availableVars, _pools)) {
        std::string error = "Buy signal expression type validation failed: " +
                           _buyParser->getValidationError();
        FATAL("{}", error);
        // 同步推给前端：策略初始化发生在回测请求里，只回 HTTP 500 的话前端只能
        // 看到一句泛化的 "Failed to initialize strategy"，定位不到具体是哪个变量。
        strategy_error("", error);
        delete _buyParser;
        _buyParser = nullptr;
        throw std::runtime_error(error);
    }

    DEBUG_INFO("[SignalNode:{}] stage: parse sell", _id);

    // 解析并验证卖出公式
    if (!_sellParser) {
        _sellParser = new FormulaParser(_server);
    }
    if (!_sellParser->parse(sellSignal, TradeAction::SELL)) {
        WARN("parse sell express fail.");
        delete _sellParser;
        _sellParser = nullptr;
        throw std::runtime_error("Failed to parse sell signal expression");
    }
    DEBUG_INFO("[SignalNode:{}] stage: validate sell", _id);
    // 新增：类型验证 (带 symbols 以便 short-name 正确解析)
    if (!_sellParser->validate(availableVars, _pools)) {
        std::string error = "Sell signal expression type validation failed: " +
                           _sellParser->getValidationError();
        FATAL("{}", error);
        strategy_error("", error);
        delete _sellParser;
        _sellParser = nullptr;
        throw std::runtime_error(error);
    }

    return true;
}

NodeProcessResult SignalNode::Process(const String& strategy, DataContext& context)
{
    // INFO("[SignalNode:{}] Process ENTER, pools={}, buy='{}', sell='{}'",
    //      _id, _pools.size(), _buyExpression, _sellExpression);

    Set<String> args;
    for (auto& item: _ins) {
        auto names = item.second->out_elements();
        for (auto& info: names) {
            args.insert(info.first);
        }
    }

    // INFO("[SignalNode:{}] input args ({}): {}",
    //      _id, args.size(), boost::algorithm::join(args, ", "));

    auto buys = _buyParser->envoke(_pools, args, context);
    auto sells = _sellParser->envoke(_pools, args, context);

    // 诊断日志：记录 raw_strength 值（每 100 个 epoch 记录一次，避免日志过多）
    // static int epoch_counter = 0;
    // if (++epoch_counter % 100 == 0) {
    //     for (const auto& symbol : _pools) {
    //         String key = get_symbol(symbol) + ".raw_strength";
    //         if (context.exist(key)) {
    //             auto s_var = context.get<Vector<double>>(key);
    //             if (!s_var.empty()) {
    //                 double strength = s_var.back();
    //                 DEBUG_INFO("[SignalNode:{}] epoch={} {} raw_strength={:.6f}", 
    //                      _id, epoch_counter, get_symbol(symbol), strength);
    //             }
    //         }
    //     }
    // }

    // 持仓读取 + 做空门控 + 已持仓不再加仓（基类统一处理）
    Map<symbol_t, int64_t> heldSymbols = loadPositions(strategy);
    applyShortableFilter(sells, heldSymbols);
    applyNoAddOnPosition(buys, heldSymbols);
    // for (auto it = buys.begin(); it != buys.end(); ++it) {
    //     if (it->second == TradeAction::BUY) INFO("[SignalNode] {} BUY signal, held={}", get_symbol(it->first), heldSymbols.count(it->first) ? heldSymbols[it->first] : 0);
    // }
    // for (auto it = sells.begin(); it != sells.end(); ++it) {
    //     if (it->second == TradeAction::SELL) INFO("[SignalNode] {} SELL signal, held={}", get_symbol(it->first), heldSymbols.count(it->first) ? heldSymbols[it->first] : 0);
    // }
    Map<symbol_t, TradeAction> decisions;
    for (auto& trade: {buys, sells}) {
        for (auto& item: trade) {
            if (item.second != TradeAction::HOLD) {
                if (decisions.count(item.first) && decisions[item.first] != item.second) {
                    if (_server->GetRunningMode() != RuningType::Backtest) {
                        STRATEGY_IMPORTANT(strategy, "[SignalNode] {} not match operation!", item.first);
                    } else {
                        IMPORTANT("[SignalNode] {} not match operation!", item.first);
                    }
                    continue;
                }
                decisions[item.first] = item.second;
                TradeSignal *signal = new TradeSignal(item.first, item.second);
                context.AddSignal(signal);
            }
        }
    }

    // 将信号数据写入 context，供 debug 节点使用
    writeSignals(context, decisions);

    // ── DuckDB node_io 日志（仅实盘模式）──
    NODE_IO_LOG("signal", _id,
        input["buy_expression"] = _buyExpression;
        input["sell_expression"] = _sellExpression;
        nlohmann::json pools_arr = nlohmann::json::array();
        for (const auto& sym : _pools) {
            pools_arr.push_back(get_symbol(sym));
        }
        input["pools"] = pools_arr;

        nlohmann::json sig_arr = nlohmann::json::array();
        for (const auto& symbol : _pools) {
            String key = get_symbol(symbol) + ".signal";
            int signal_value = 0;
            if (context.exist(key)) {
                const auto& sig_vec = context.get<Vector<double>>(key);
                if (!sig_vec.empty()) signal_value = static_cast<int>(sig_vec.back());
            }
            nlohmann::json sig_entry;
            sig_entry["symbol"] = get_symbol(symbol);
            sig_entry["signal"] = signal_value;
            sig_arr.push_back(sig_entry);
        }
        output["signals"] = sig_arr;
    );

    return NodeProcessResult::Success;
}

bool SignalNode::ParseBuyExpression(const String& expression) {
    if (!_buyParser) {
        _buyParser = new FormulaParser(_server);
    }
    return _buyParser->parse(expression, TradeAction::BUY);
}

bool SignalNode::ParseSellExpression(const String& expression) {
    if (!_sellParser) {
        _sellParser = new FormulaParser(_server);
    }
    return _sellParser->parse(expression, TradeAction::SELL);
}

SignalNode::~SignalNode() {
    if (_buyParser) {
        delete _buyParser;
    }
    if (_sellParser) {
        delete _sellParser;
    }
}

const nlohmann::json SignalNode::getParams() {
    return {"buy", "sell", "allowShort"};
}