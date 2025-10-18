#include "StrategyNode.h"
#include "Interprecter/Stmt.h"
#include "Function/Function.h"

feature_t InputNode::Process(const DataFeatures& org, const feature_t& input)
{
    return input;
}

void InputNode::Connect(QNode* next, const String& from, const String& to) {
    QNode::Connect(next, from, to);
}

feature_t OperationNode::Process(const DataFeatures& org, const feature_t& input)
{
    return input;
}

bool OperationNode::parseFomula(const String& formulas) {
    FormulaParser parser(nullptr);
    return parser.parse(formulas);
}

feature_t StatisticNode::Process(const DataFeatures& org, const feature_t& input)
{
    return input;
}

feature_t FeatureNode::Process(const DataFeatures& org, const feature_t& input)
{
    return input;
}

feature_t FunctionNode::Process(const DataFeatures& org, const feature_t& input)
{
    
    return input;
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

SignalNode::SignalNode(Server* server):_server(server) {

}

feature_t SignalNode::Process(const DataFeatures& org, const feature_t& input)
{
    return input;
}
