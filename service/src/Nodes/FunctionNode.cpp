#include "Nodes/FunctionNode.h"
#include "StrategyNode.h"
#include "Util/string_algorithm.h"
#include "DataGroup.h"
#include "server.h"
#include "boost/algorithm/string/join.hpp"
#include "boost/algorithm/string/replace.hpp"
#include "Function/Function.h"
#include "Function/Normalization.h"
#include "Util/finance.h"
#include <algorithm>

#define ADD_ARGUMENT(type, name) { type v = data["params"][name]["value"]; node->AddArgument(name, v);}

List<String> FunctionNode::GetNames() {
    return {"MA", "MinMax"};
}

List<String> FunctionNode::GetParams(const String& name) {
    static Map<String, List<String>> params{
        {"MA", {"smoothTime"}},
        {"MinMax", {"lower", "upper"}},
    };
    if (params.count(name) == 0) {
        return {};
    }
    return params.at(name);
}

FunctionNode::FunctionNode(Server* server)
:_server(server) {

}

void FunctionNode::UpdateLabel(const String& label) {
    if (_label != label) {
        Map<String, ArgType> new_outputs;
        for (auto& item: _outputs) {
            String name = item.first;
            boost::algorithm::replace_all(name, _label, label);
            new_outputs[name] = item.second;
        }
        _outputs.swap(new_outputs);
        _label = label;
    }
}

bool FunctionNode::Init(const nlohmann::json& config) {
    // 从输入节点获取处理的属性
    for (auto& item: _ins) {
        auto input_names = item.second->out_elements();
        _params.merge(input_names);
    }

    Set<String> symbols;
    for (auto& item: _params) {
        auto& name = item.first;
        Vector<String> tokens;
        split(name, tokens, ".");
        tokens.erase(tokens.end());
        symbols.insert(boost::algorithm::join(tokens, "."));
    }

    String name = config["params"]["method"]["value"];
    if (name == "MA") {
        int cnt = config["params"]["smoothTime"]["value"];
        _callable = new MA(cnt);
    }
    else if (name == "MinMax") {
        // 根据输入节点的要素，获取上下限
        for (auto& item: _ins) {
            auto outs = item.second->out_elements();
            String key = outs.begin()->first;
            auto pos = key.find_last_of(".");
            auto symbol = key.substr(0, pos);
            auto prop = key.substr(pos + 1);
            // TODO: 此时数据未准备,需要自行加载,找到最大最小值
            DataFrame df;
            auto& cgf = _server->GetConfig();
            String path = cgf.GetDatabasePath() + "/" + symbol + "_hist_data.csv";
            if (!LoadStockQuote(df, path)) {
                FATAL("stock csv {} not exist.", path);
                return false;
            }
            
            auto& data = df.get_column<double>(prop.c_str());
            auto upper = *std::max_element(data.begin(), data.end());
            auto lower = *std::min_element(data.begin(), data.end());
            _callable = new MinMax(lower, upper);
            break;
        }
    }
    
    _label = (String)config["label"];
    for (auto& symbol: symbols) {
        _outputs[symbol + "." + _label] = ArgType::Double;
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
        if (context.exist(item.first)) {
            context.add(item.first, std::get<double>(result));
        } else {
            Vector<double> timeseriel;
            timeseriel.push_back(std::get<double>(result));
            context.set(item.first, timeseriel);
        }
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

