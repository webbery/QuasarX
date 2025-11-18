#include "Nodes/FunctionNode.h"
#include "Function/Function.h"

bool FunctionNode::Init(DataContext& context, const nlohmann::json& config) {
    if (_funcionName == "MA") {
        auto cnt = std::get<int>(_args["smoothTime"]);
        _callable = new MA(cnt);
    }
    return true;
}

bool FunctionNode::Process(const String& strategy, DataContext& context)
{
    if (!_callable) {[[unlikely]]
        throw std::invalid_argument("Node: function is not set");
    }
    // return (*_callable)(input);
    return true;
}

FunctionNode::~FunctionNode() {
    if (_callable)
        delete _callable;
}