#include "Nodes/SignalNode.h"
#include "Interprecter/Stmt.h"
#include "Util/log.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include <utility>

SignalNode::SignalNode(Server* server):_server(server), _buyParser(nullptr), _sellParser(nullptr) {
}

bool SignalNode::Init(const nlohmann::json& config) {
    auto& buySignal = config["params"]["buy"]["value"];
    auto& sellSignal = config["params"]["sell"]["value"];

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

    auto& operatorPool = config["params"]["code"]["value"];
    for (String code: operatorPool) {
        auto& security = Server::GetSecurity(code);
        auto symbol = to_symbol(code, security);
        _pools.emplace_back(symbol);
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

    auto buys = _buyParser->envoke(_pools, args, context);
    auto sells = _sellParser->envoke(_pools, args, context);

    Map<symbol_t, TradeAction> decisions;
    for (auto& trade: {buys, sells}) {
        for (auto& item: trade) {
            if (item.second != TradeAction::HOLD) {
                if (decisions.count(item.first) && decisions[item.first] != item.second) {
                    INFO("{} not match operation!", item.first);
                    continue;
                }
                decisions[item.first] = item.second;
                TradeSignal *signal = new TradeSignal(item.first, item.second);
                context.AddSignal(signal);
                INFO("{} TradeSignal {}", item.first, (int)item.second - 1);
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
            context.set(key, Vector<double>{(double)signalValue});
        }
    }
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
    return {"buy", "sell", "code"};
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