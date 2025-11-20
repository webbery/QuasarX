#include "StrategyNode.h"
#include "Interprecter/Stmt.h"
#include "Function/Function.h"
#include <stdexcept>

Map<String, ArgType> QNode::out_elements() {
    Map<String, ArgType> elems;
    return elems;
}

bool OperationNode::Init(DataContext& context, const nlohmann::json& config) {
    return true;
}

bool OperationNode::Process(const String& strategy, DataContext& context)
{
    return true;
}

bool OperationNode::parseFomula(const String& formulas) {
    FormulaParser parser(nullptr);
    return parser.parse(formulas);
}

bool FeatureNode::Process(const String& strategy, DataContext& context)
{
    return true;
}

SignalNode::SignalNode(Server* server):_server(server), _buyParser(nullptr), _sellParser(nullptr) {
}

bool SignalNode::Init(DataContext& context, const nlohmann::json& config) {
    return true;
}

bool SignalNode::Process(const String& strategy, DataContext& context)
{
    // List<String> args;
    // for (auto& item: _outs) {
    //     auto& name = item.first;
    //     args.push_back(name);
    // }
    // auto buys = _buyParser->envoke(org._symbols, args, &context);
    // auto sells = _sellParser->envoke(org._symbols, args, &context);
    // List<TradeDecision> decisions(buys);
    // decisions.splice(decisions.end(), sells);
    // auto broker = _server->GetBrokerSubSystem();
    // // broker->RegistIndicator(, StatisticIndicator::Sharp);
    // for (auto& decision: decisions) {
    //     Order order;
    //     if (decision.action == TradeAction::BUY) {
    //         broker->Buy(strategy, decision.symbol, order, [symbol = decision.symbol] (const TradeReport& report) {
    //             auto sock = Server::GetSocket();
    //             auto info = to_sse_string(symbol, report);
    //             nng_send(sock, info.data(), info.size(), 0);
    //         });
    //     }
    //     else if (decision.action == TradeAction::SELL) {
    //         broker->Sell(strategy, decision.symbol, order, [symbol = decision.symbol] (const TradeReport& report) {
    //             auto sock = Server::GetSocket();
    //             auto info = to_sse_string(symbol, report);
    //             nng_send(sock, info.data(), info.size(), 0);
    //         });
    //     }
    // }
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
