#include "StrategyNode.h"
#include "Interprecter/Stmt.h"
#include "Function/Function.h"
#include <stdexcept>

bool OperationNode::Process(const String& strategy, DataContext& context, const DataFeatures& org)
{
    return true;
}

bool OperationNode::parseFomula(const String& formulas) {
    FormulaParser parser(nullptr);
    return parser.parse(formulas);
}

bool StatisticNode::Process(const String& strategy, DataContext& context, const DataFeatures& org)
{
    for (auto node: _ins) {
        auto& feature = context.get(node.first);
    }
    for (auto indicator: _indicators) {
        switch (indicator) {
        case StatisticIndicator::Sharp:
        break;
        default:
        break;
        }
    }
    return true;
}

bool FeatureNode::Process(const String& strategy, DataContext& context, const DataFeatures& org)
{
    return true;
}

bool FunctionNode::Process(const String& strategy, DataContext& context, const DataFeatures& org)
{
    if (!_callable) {[[unlikely]]
        if (!Init()) {
            throw std::invalid_argument("Node: function is not set");
        }
    }
    // return (*_callable)(input);
    return true;
}

bool FunctionNode::Init() {
    if (_funcionName == "MA") {
        _callable = new MA();
    }
    return true;
}

FunctionNode::~FunctionNode() {
    if (_callable)
        delete _callable;
}

SignalNode::SignalNode(Server* server):_server(server), _buyParser(nullptr), _sellParser(nullptr) {

}

bool SignalNode::Process(const String& strategy, DataContext& context, const DataFeatures& org)
{
    List<String> args;
    for (auto& item: _outs) {
        auto& name = item.first;
        args.push_back(name);
    }
    auto buys = _buyParser->envoke(org._symbols, args, &context);
    auto sells = _sellParser->envoke(org._symbols, args, &context);
    List<TradeDecision> decisions(buys);
    decisions.splice(decisions.end(), sells);
    auto broker = _server->GetBrokerSubSystem();
    // broker->RegistIndicator(, StatisticIndicator::Sharp);
    for (auto& decision: decisions) {
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

bool SignalNode::parseFomula(const String& formulas) {
    if (!_buyParser) {
        _buyParser = new FormulaParser(_server);
    }
    if (!_sellParser) {
        _sellParser = new FormulaParser(_server);
    }
    return _buyParser->parse(formulas, TradeAction::BUY) && _sellParser->parse(formulas, TradeAction::SELL);
}

SignalNode::~SignalNode() {
    if (_buyParser) {
        delete _buyParser;
    }
    if (_sellParser) {
        delete _sellParser;
    }
}
