#include "StrategyNode.h"
#include "Interprecter/Stmt.h"
#include "Function/Function.h"
#include <stdexcept>
#include <variant>

feature_t& DataContext::get(const String& name) {
    return _outputs[name];
}

void DataContext::add(const String& name, double value) {
    std::get<Vector<double>>(_outputs[name]).push_back(value);
}

bool DataContext::exist(const String& name) {
    return _outputs.contains(name);
}

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
