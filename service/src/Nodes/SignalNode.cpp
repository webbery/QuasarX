#include "Nodes/SignalNode.h"
#include "Interprecter/Stmt.h"
#include "server.h"
#include "BrokerSubSystem.h"

SignalNode::SignalNode(Server* server):_server(server), _buyParser(nullptr), _sellParser(nullptr) {
}

bool SignalNode::Init(const nlohmann::json& config) {
    auto& buySignal = config["params"]["buy"]["value"];
    auto& sellSignal = config["params"]["sell"]["value"];
    if (!_buyParser) {
        _buyParser = new FormulaParser(_server);
    }
    if (!_buyParser->parse(buySignal, TradeAction::BUY)) {
        WARN("parse buy express fail.");
        delete _buyParser;
        _buyParser = nullptr;
        return false;
    }
    if (!_sellParser) {
        _sellParser = new FormulaParser(_server);
    }
    if (!_sellParser->parse(sellSignal, TradeAction::SELL)) {
        WARN("parse sell express fail.");
        delete _sellParser;
        _sellParser = nullptr;
        return false;
    }
    auto& operatorPool = config["params"]["code"]["value"];
    for (String code: operatorPool) {
        auto& security = Server::GetSecurity(code);
        auto symbol = to_symbol(code, security);
        _pools.emplace_back(symbol);
    }

    return true;
}

bool SignalNode::Process(const String& strategy, DataContext& context)
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
    Map<symbol_t, TradeDecision> decisions;
    for (auto& trade: {buys, sells}) {
        for (auto& item: trade) {
            if (item.action != TradeAction::HOLD) {
                if (decisions.count(item.symbol) && decisions[item.symbol].action != item.action) {
                    INFO("not match operation!");
                    continue;
                }
                decisions[item.symbol] = item;
            }
        }
    }
    
    auto broker = _server->GetBrokerSubSystem();
    for (auto& item: decisions) {
        auto& decision = item.second;
        Order order;
        if (decision.action == TradeAction::BUY) {
            broker->Buy(strategy, decision.symbol, order, [symbol = decision.symbol] (const TradeReport& report) {
                auto sock = Server::GetSocket();
                auto info = to_sse_string(symbol, report);
                nng_send(sock, info.data(), info.size(), 0);
            });
        }
        else if (decision.action == TradeAction::SELL) {
            broker->Sell(strategy, decision.symbol, order, [symbol = decision.symbol] (const TradeReport& report) {
                auto sock = Server::GetSocket();
                auto info = to_sse_string(symbol, report);
                nng_send(sock, info.data(), info.size(), 0);
            });
        }
    }
    return true;
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
