#include "Nodes/FunctionNode.h"
#include "StrategyNode.h"
#include "Util/string_algorithm.h"
#include "server.h"
#include "boost/algorithm/string/join.hpp"
#include "boost/algorithm/string/replace.hpp"
#include "boost/core/span.hpp"
#include "Function/Function.h"
#include "Function/Normalization.h"
#include "Util/finance.h"
#include <algorithm>
#include <stdexcept>

#define ADD_ARGUMENT(type, name) { type v = data["params"][name]["value"]; node->AddArgument(name, v);}

namespace {
    static const Map<String, int> timeHorizon{
        {"6s", 6}, {"30s", 30}, {"1m", 60}, {"5m", 300}, {"1h", 3600}, {"1d", 1}, {"3d", 3}, {"5d", 5}, 
    };

    Map<String, std::function<ICallable* (const FunctionNode&, const nlohmann::json&)>> intrinsic_functions{
        {"MA", [] (const FunctionNode& node, const nlohmann::json& config) {
            int cnt = config["params"]["smoothTime"]["value"];
            return new MA(cnt);
        }},
        {"MinMax", [] (const FunctionNode& node, const nlohmann::json& config) -> ICallable* {
            // 根据输入节点的要素，获取上下限
            for (auto& item: node.ins()) {
                auto outs = item.second->out_elements();
                auto& key = outs.begin()->first;
                Vector<String> tokens;
                split(key, tokens, ".");
                boost::span<String> spanview(tokens);
                // TODO: 此时数据未准备,需要自行加载,找到最大最小值
                auto symbol = to_symbol(boost::algorithm::join(spanview.subspan(0, 2), "."));
                auto& cgf = node.GetServer()->GetConfig();
                DataFrame df;
                if (is_stock(symbol)) {
                    String path = cgf.GetDatabasePath() + "/A_hfq/" + spanview[1] + "_hist_data.csv";
                    if (!LoadStockQuote(df, path)) {
                        String info = fmt::format("stock csv {} not exist.", path);
                        INFO("{}", info);
                        throw std::runtime_error(info.c_str());
                    }
                }
                else {
                    
                }
                
                auto& data = df.get_column<double>(spanview[2].c_str());
                auto upper = *std::max_element(data.begin(), data.end());
                auto lower = *std::min_element(data.begin(), data.end());
                return new MinMax(lower, upper);
            }
            return nullptr;
        }},
        {"Z-score", [] (const FunctionNode& node, const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"ATR", [] (const FunctionNode& node, const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"VWAP", [] (const FunctionNode& node, const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"RSI", [] (const FunctionNode& node, const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"STD", [] (const FunctionNode& node, const nlohmann::json& config) -> ICallable* {
            String cnt = (String)config["params"]["range"]["value"];
            return new STD(timeHorizon.at(cnt));
        }},
        {"Return", [] (const FunctionNode& node, const nlohmann::json& config) -> ICallable* {
            String cnt = (String)config["params"]["range"]["value"];
            return new Return(timeHorizon.at(cnt));
        }},
        {"R2", [] (const FunctionNode& node, const nlohmann::json& config) -> ICallable* {
            String cnt = (String)config["params"]["range"]["value"];
            return new R2(timeHorizon.at(cnt));
        }},
        {"ZScore", [] (const FunctionNode& node, const nlohmann::json& config) -> ICallable* {
            String cnt = (String)config["params"]["range"]["value"];
            return new ZScore(timeHorizon.at(cnt));
        }},
    };
}

List<String> GetAllFunctionNames() {
    List<String> names;
    for (auto& item: intrinsic_functions) {
        names.push_back(item.first);
    }
    return names;
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

    // 构建输入到输出的映射，并收集所有 symbol
    _label = (String)config["label"];
    for (auto& item: _params) {
        auto& name = item.first;
        Vector<String> tokens;
        split(name, tokens, ".");
        tokens.pop_back();  // 去掉属性名
        String symbol = boost::algorithm::join(tokens, ".");
        
        // 建立映射: "sh600519.close" -> "sh600519.ReturnRate"
        _param_to_output_map[name] = symbol + "." + _label;
    }

    String name = config["params"]["method"]["value"];
    auto itr = intrinsic_functions.find(name);
    if (itr == intrinsic_functions.end()) {
        String info = fmt::format("function {} not implement.", name);
        throw std::runtime_error(info.c_str());
    }
    _callable = itr->second(*this, config);

    for (auto& [input_key, output_key] : _param_to_output_map) {
        // 函数输出是时间序列
        _outputs[output_key] = ArgType::Double_TimeSeries;
    }
    return true;
}

NodeProcessResult FunctionNode::Process(const String& strategy, DataContext& context)
{
    if (!_callable) {[[unlikely]]
        WARN("Node: function is not set");
        return NodeProcessResult::Error;
    }

    // 1. 收集所有 symbol 的输入数据
    Map<String, context_t> arguments;
    Vector<String> output_keys;  // 保存输出 key 的顺序
    
    for (auto& item: _params) {
        auto& value = context.get(item.first);
        arguments[item.first] = value;
    }
    
    // 2. 按 _param_to_output_map 的顺序构建输出 key 列表
    for (auto& [input_key, output_key] : _param_to_output_map) {
        output_keys.push_back(output_key);
    }
    
    // 3. 调用函数计算
    auto result = (*_callable)(arguments);
    
    // 4. 根据结果类型处理，按顺序写入输出
    auto ret = std::visit([this, &output_keys, &context, &strategy](const auto& val) -> NodeProcessResult {
        using T = std::decay_t<decltype(val)>;
        
        if constexpr (std::is_same_v<T, double>) {
            // 标量：所有输出 key 都用同一个值
            for (auto& key : output_keys) {
                if (context.exist(key)) {
                    context.add(key, val);
                } else {
                    Vector<double> timeseries;
                    timeseries.push_back(val);
                    context.set(key, timeseries);
                }
            }
        } else if constexpr (std::is_same_v<T, Vector<double>>) {
            // 向量：按顺序分配给每个输出 key
            if (val.size() != output_keys.size()) {
                WARN("Function result size {} != output keys size {}", val.size(), output_keys.size());
                return NodeProcessResult::Skip;
            }
            for (size_t i = 0; i < output_keys.size(); ++i) {
                if (context.exist(output_keys[i])) {
                    context.add(output_keys[i], val[i]);
                } else {
                    Vector<double> timeseries;
                    timeseries.push_back(val[i]);
                    context.set(output_keys[i], timeseries);
                }
            }
        } else {
            WARN("Function returned unsupported type");
            return NodeProcessResult::Error;
        }
        
        return NodeProcessResult::Success;
    }, result);
    
    return ret;
}

Map<String, ArgType> FunctionNode::out_elements() {
    return _outputs;
}

FunctionNode::~FunctionNode() {
    if (_callable)
        delete _callable;
}

const nlohmann::json FunctionNode::getParams() {
    nlohmann::json params;
    for (auto& item: intrinsic_functions) {
        auto key = item.first;
        params[key] = {{"args", "type"}};
    }
    return params;
}