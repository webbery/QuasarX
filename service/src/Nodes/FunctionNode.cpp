#include "Nodes/FunctionNode.h"
#include "Function/Function.h"
#include "StrategyNode.h"
#include "Util/string_algorithm.h"

#define ADD_ARGUMENT(type, name) { type v = data["params"][name]["value"]; node->AddArgument(name, v);}

bool FunctionNode::Init(const nlohmann::json& config) {
    String name = config["params"]["method"]["value"];
    if (name == "MA") {
        int cnt = config["params"]["smoothTime"]["value"];
        _callable = new MA(cnt);

        String label = config["label"];
        _outputs[label] = ArgType::Double;
    }
    // 从输入节点获取处理的属性
    for (auto& item: _ins) {
        auto input_names = item.second->out_elements();
        _params.merge(input_names);
    }
    return true;
}

bool FunctionNode::Process(const String& strategy, DataContext& context)
{
    if (!_callable) {[[unlikely]]
        WARN("Node: function is not set");
        return false;
    }

    Map<String, feature_t> arguments;
    for (auto& item: _params) {
        auto& value = context.get(item.first);
        arguments[item.first] = value;
    }
    auto result = (*_callable)(arguments);
    for (auto& item: _outputs) {
        context.add(item.first, result);
    }
    return true;
}

Map<String, ArgType> FunctionNode::out_elements() {
    return _outputs;
}

FunctionNode::~FunctionNode() {
    if (_callable)
        delete _callable;
}