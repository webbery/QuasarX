#include "StrategyNode.h"
#include "Interprecter/Stmt.h"


List<QNode*> InputNode::Process(const List<QNode*>& input)
{
    return input;
}

void InputNode::Connect(QNode* next, const String& from, const String& to) {
    QNode::Connect(next, from, to);
}

bool InputNode::parseFormula(const String& formulas) {
    FormulaParser parser(nullptr);
    return parser.parse(formulas);
}

List<QNode*> OperationNode::Process(const List<QNode*>& input)
{
    return input;
}

List<QNode*> OutputNode::Process(const List<QNode*>& input)
{
    return input;
}

List<QNode*> FeatureNode::Process(const List<QNode*>& input)
{
    return input;
}

List<QNode*> FunctionNode::Process(const List<QNode*>& input)
{
    return input;
}