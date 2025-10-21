#include "StrategyNode.h"
#include "Interprecter/Stmt.h"
#include "Function/Function.h"
#include <stdexcept>

bool OperationNode::Process(DataContext& context, const DataFeatures& org)
{
    return true;
}

bool OperationNode::parseFomula(const String& formulas) {
    FormulaParser parser(nullptr);
    return parser.parse(formulas);
}

bool StatisticNode::Process(DataContext& context, const DataFeatures& org)
{
    return true;
}

bool FeatureNode::Process(DataContext& context, const DataFeatures& org)
{
    return true;
}

bool FunctionNode::Process(DataContext& context, const DataFeatures& org)
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

SignalNode::SignalNode(Server* server):_server(server) {

}

bool SignalNode::Process(DataContext& context, const DataFeatures& org)
{
    return true;
}
