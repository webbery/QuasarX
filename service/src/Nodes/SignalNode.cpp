#include "Nodes/SignalNode.h"
#include "DataContext.h"
#include "ExchangeManager.h"
#include "Interprecter/Stmt.h"
#include "Util/log.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Nodes/QuoteNode.h"
#include <utility>
#include "boost/algorithm/string.hpp"

SignalNode::SignalNode(Server* server):_server(server), _buyParser(nullptr), _sellParser(nullptr) {
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
    // 新增：类型验证
    if (!_buyParser->validate(availableVars)) {
        std::string error = "Buy signal expression type validation failed: " +
                           _buyParser->getValidationError();
        FATAL("{}", error);
        delete _buyParser;
        _buyParser = nullptr;
        throw std::runtime_error(error);
    }

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
    // 新增：类型验证
    if (!_sellParser->validate(availableVars)) {
        std::string error = "Sell signal expression type validation failed: " +
                           _sellParser->getValidationError();
        FATAL("{}", error);
        delete _sellParser;
        _sellParser = nullptr;
        throw std::runtime_error(error);
    }

    // 沿路径回溯，找到所有可达的 QuoteInputNode（数据源的真实入口）
    // 使用 BFS 遍历所有上游节点，收集所有 QuoteInputNode 的 symbol
    Set<symbol_t> upstreamSymbols;
    Set<QNode*> visited;
    Vector<QNode*> queue;

    // 从直接上游节点开始 BFS
    for (auto& item: _ins) {
        queue.push_back(item.second);
    }

    while (!queue.empty()) {
        QNode* current = queue.back();
        queue.pop_back();

        if (visited.count(current)) continue;
        visited.insert(current);

        // 如果是 QuoteInputNode，提取其 symbol
        if (auto* quoteNode = dynamic_cast<QuoteInputNode*>(current)) {
            const auto& symbols = quoteNode->GetSymbols();
            for (const auto& sym : symbols) {
                upstreamSymbols.insert(sym);
            }
        }

        // 继续向上游遍历
        for (auto& item: current->ins()) {
            if (!visited.count(item.second)) {
                queue.push_back(item.second);
            }
        }
    }

    if (upstreamSymbols.empty()) {
        WARN("[SignalNode:{}] No upstream symbols found from QuoteInputNode (traversed {} nodes), signal node will be skipped", _id, visited.size());
        return false;
    }

    INFO("[SignalNode:{}] Found {} symbols from {} upstream nodes (traversed {} nodes)",
         _id, upstreamSymbols.size(), _ins.size(), visited.size());

    for (const auto& sym : upstreamSymbols) {
        _pools.emplace_back(sym);
    }

    if (config["params"].contains("allowShort")) {
        _allowShort = config["params"]["allowShort"]["value"];
    }

    return true;
}

NodeProcessResult SignalNode::Process(const String& strategy, DataContext& context)
{
    Set<String> args;
    for (auto& item: _ins) {
        auto names = item.second->out_elements();
        for (auto& info: names) {
            args.insert(info.first);
        }
    }

    // INFO("[SignalNode:{}] Processing, pools={}, args={}", 
    //      _id, _pools.size(), boost::algorithm::join(args, ", "));

    auto buys = _buyParser->envoke(_pools, args, context);
    auto sells = _sellParser->envoke(_pools, args, context);
    // 如果不允许做空，过滤无持仓标的的 SELL 信号
    Map<symbol_t, int64_t> heldSymbols;
    if (_server->GetRunningMode() == RuningType::Backtest) {
        auto* exchangeMgr = _server->GetExchangeManager();
        for (const auto& symbol : _pools) {
            auto* histExchange = dynamic_cast<HistorySimulationBase*>(exchangeMgr->ResolveExchange(symbol));
            if (histExchange) {
                auto cnt = histExchange->GetPositionQuantity(symbol);
                if (cnt != 0) {
                    heldSymbols[symbol] = cnt;
                }
            }
        }
    } else {
        auto& positions = _server->GetPosition("");
        for (const auto& pos : positions._positions) {
            if (pos._holds != 0) {
                heldSymbols[pos._symbol] = pos._holds;
            }
        }
    }
    if (!_allowShort) {
        for (auto it = sells.begin(); it != sells.end(); ++it) {
            if (it->second == TradeAction::SELL && !heldSymbols.count(it->first)) {
                if (_server->GetRunningMode() != RuningType::Backtest) {
                    STRATEGY_IMPORTANT(strategy, "[SignalNode] SELL signal for {} dropped: no position held (_allowShort=false)", get_symbol(it->first));
                } else {
                    IMPORTANT("[SignalNode] SELL signal for {} dropped: no position held (_allowShort=false)", get_symbol(it->first));
                }
                it->second = TradeAction::HOLD;
            }
        }
    }
    // 如果已经持仓,不再追加买入
    for (auto& item: buys) {
        if (item.second == TradeAction::BUY && heldSymbols.count(item.first)) {
            item.second = TradeAction::HOLD;
        }
    }
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
    // 信号值：1=买入，-1=卖出，0=持有
    for (auto& symbol : _pools) {
        String key = get_symbol(symbol) + ".signal";
        int signalValue = 0;  // 默认持有
        if (decisions.count(symbol)) {
            if (decisions[symbol] == TradeAction::BUY) {
                signalValue = 1;
            } else if (decisions[symbol] == TradeAction::SELL) {
                signalValue = -1;
            }
        }
        // 检查是否已存在，存在则追加，否则创建新向量
        if (context.exist(key)) {
            context.add(key, (double)signalValue);
        } else {
            // 首次写入，预填充 warmup 占位符
            // warmup 期间 SignalNode 被跳过，但 QuoteInputNode 仍然写入了时间
            // 需要预填充 warmup 个 0 值，使数据长度与时间序列对齐
            int warmup = context.GetWarmupEpochs();
            Vector<double> signalVec;
            if (warmup > 0) {
                signalVec.assign(warmup, 0.0);  // warmup 期间信号为 0（持有）
            }
            signalVec.push_back((double)signalValue);
            context.set(key, std::move(signalVec));
        }
    }

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

Map<String, ArgType> SignalNode::out_elements() {
    Map<String, ArgType> elems;
    // 输出信号数据，格式为 "{symbol}.signal"
    for (auto& symbol : _pools) {
        // 信号是整数时间序列
        elems[get_symbol(symbol) + ".signal"] = ArgType::Integer_TimeSeries;
    }
    return elems;
}