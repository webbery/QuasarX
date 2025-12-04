#include "StrategyNode.h"
#include "Interprecter/Stmt.h"
#include "Function/Function.h"
#include <stdexcept>
#include <variant>


Map<String, ArgType> QNode::out_elements() {
    Map<String, ArgType> elems;
    return elems;
}

bool OperationNode::Init(const nlohmann::json& config) {
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
