#include "StrategyNode.h"
#include "Interprecter/Stmt.h"


feature_t InputNode::Process(const feature_t& input)
{
    return input;
}

void InputNode::Connect(QNode* next, const String& from, const String& to) {
    QNode::Connect(next, from, to);
}

feature_t OperationNode::Process(const feature_t& input)
{
    return input;
}

bool OperationNode::parseFomula(const String& formulas) {
    FormulaParser parser(nullptr);
    return parser.parse(formulas);
}

feature_t OutputNode::Process(const feature_t& input)
{
    return input;
}

feature_t FeatureNode::Process(const feature_t& input)
{
    return input;
}

feature_t FunctionNode::Process(const feature_t& input)
{
    return input;
}