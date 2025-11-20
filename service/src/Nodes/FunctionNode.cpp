#include "Nodes/FunctionNode.h"
#include "Function/Function.h"
#include "Util/string_algorithm.h"

bool FunctionNode::Init(DataContext& context, const nlohmann::json& config) {
    if (_funcionName == "MA") {
        auto cnt = std::get<int>(_args["smoothTime"]);
        _callable = new MA(cnt);
        // 从输入节点获取处理的属性
        for (auto& item: _ins) {
            auto input_names = item.second->out_elements();
            _params.emplace(input_names.begin(), input_names.end());
        }
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
    
    return true;
}

Map<String, ArgType> FunctionNode::out_elements() {
    Map<String, ArgType> names;
    return names;
}

FunctionNode::~FunctionNode() {
    if (_callable)
        delete _callable;
}