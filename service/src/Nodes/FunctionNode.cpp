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
#include <regex>
#include <stdexcept>

#define ADD_ARGUMENT(type, name) { type v = data["params"][name]["value"]; node->AddArgument(name, v);}

namespace {
    static const Map<String, int> timeHorizon{
        {"6s", 6}, {"30s", 30}, {"1m", 60}, {"5m", 300}, {"1h", 3600}, {"1d", 1}, {"3d", 3}, {"5d", 5}, {"15d", 15},
    };

    using CallableFactory = std::function<ICallable*(const nlohmann::json&)>;

    Map<String, CallableFactory> intrinsic_functions{
        {"MA", [] (const nlohmann::json& config) {
            String range = config["params"]["range"]["value"].dump();
            std::regex re(R"(\d+)");
            std::smatch match;
            if (std::regex_search(range, match, re)) {
                int period = std::stoi(match.str());
                if (period <= 0) {
                    throw std::runtime_error("MA range period must be positive, got: " + range);
                }
                return new MA(period);
            }
            throw std::runtime_error("Invalid MA range format: " + range);
        }},
        {"MinMax", [] (const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"Z-score", [] (const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"ATR", [] (const nlohmann::json& config) -> ICallable* {
            String cnt = (String)config["params"]["range"]["value"];
            return new ATR(timeHorizon.at(cnt));
        }},
        {"VWAP", [] (const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"RSI", [] (const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"STD", [] (const nlohmann::json& config) -> ICallable* {
            String cnt = (String)config["params"]["range"]["value"];
            return new STD(timeHorizon.at(cnt));
        }},
        {"Return", [] (const nlohmann::json& config) -> ICallable* {
            String cnt = (String)config["params"]["range"]["value"];
            return new Return(timeHorizon.at(cnt));
        }},
        {"R2", [] (const nlohmann::json& config) -> ICallable* {
            String cnt = (String)config["params"]["range"]["value"];
            return new R2(timeHorizon.at(cnt));
        }},
        {"ZScore", [] (const nlohmann::json& config) -> ICallable* {
            String cnt = (String)config["params"]["range"]["value"];
            return new ZScore(timeHorizon.at(cnt));
        }},
        {"VPCorr", [] (const nlohmann::json& config) -> ICallable* {
            String cnt = (String)config["params"]["range"]["value"];
            return new VPCorr(timeHorizon.at(cnt));
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

// 从连接信息中解析实际输入映射
// 遍历 _ins，对每个上游节点查找其 _outs 中连接到本节点的 sourceHandle
// 返回: slot → context key 的映射
Map<String, String> FunctionNode::resolveInputConnections() {
    Map<String, String> slotToKey;
    
    for (auto& [targetHandle, upstreamNode] : _ins) {
        // 遍历上游节点的 _outs，找到连接到本节点的 sourceHandle
        for (auto& [sourceHandle, downstreamNode] : upstreamNode->outs()) {
            if (downstreamNode != this) continue;
            
            auto outs = upstreamNode->out_elements();
            
            // 从 sourceHandle 提取数据名（去掉 nodeId 前缀）
            // 格式: "11-IMF_0" → "IMF_0", "1-close" → "close", "2" → ""
            String dataName;
            auto dashPos = sourceHandle.find('-');
            if (dashPos != String::npos) {
                dataName = sourceHandle.substr(dashPos + 1);
            }
            
            if (!dataName.empty()) {
                // 有明确的数据名，从 out_elements 中匹配 context key
                String contextKey;
                for (auto& [key, type] : outs) {
                    if (key.find(dataName) != String::npos) {
                        contextKey = key;
                        break;
                    }
                }
                if (contextKey.empty()) continue;
                
                // 根据数据名推断 slot
                if (dataName == "volume") {
                    slotToKey["volume"] = contextKey;
                } else {
                    if (slotToKey.find("price") == slotToKey.end()) {
                        slotToKey["price"] = contextKey;
                    }
                }
            } else {
                // sourceHandle 仅为节点 ID，无明确数据名
                // 如果上游只有一个输出，直接使用
                if (outs.size() == 1) {
                    auto& [key, type] = *outs.begin();
                    // 提取字段名（key 格式: "symbol.field" 或 "label.field"）
                    String fieldName;
                    auto dotPos = key.rfind('.');
                    if (dotPos != String::npos) {
                        fieldName = key.substr(dotPos + 1);
                    }
                    if (fieldName == "volume") {
                        slotToKey["volume"] = key;
                    } else {
                        if (slotToKey.find("price") == slotToKey.end()) {
                            slotToKey["price"] = key;
                        }
                    }
                }
                // 多个输出时需要明确的 sourceHandle，跳过
            }
        }
    }
    
    return slotToKey;
}

FunctionNode::~FunctionNode() {
    for (auto& [sym, callable] : _callables) {
        delete callable;
    }
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
    // 1. 从输入节点获取所有输出要素
    DEBUG_INFO("[FunctionNode:{}] Init: _ins size = {}", _id, _ins.size());
    for (auto& item: _ins) {
        auto input_names = item.second->out_elements();
        DEBUG_INFO("[FunctionNode:{}] Init: input node '{}' provided {} elements",
             _id, item.first, input_names.size());
        _params.merge(input_names);
    }

    _label = (String)config["label"];
    DEBUG_INFO("[FunctionNode:{}] Init: label='{}', _params size = {}",
         _id, _label, _params.size());

    // 2. 获取方法名
    String methodName = config["params"]["method"]["value"];

    // 3. 从 _params 的 key 中提取所有 symbol
    //    key 格式: "sz.800001.close" → symbol = "sz.800001"
    Set<String> symbolSet;
    for (auto& [key, type] : _params) {
        Vector<String> tokens;
        split(key, tokens, ".");
        if (tokens.size() < 2) continue;
        // 去掉最后一个 token（字段名），剩余部分拼接为 symbol
        tokens.pop_back();
        String symbol = boost::algorithm::join(tokens, ".");
        symbolSet.insert(symbol);
    }

    // 4. 为每个 symbol 创建独立的 callable 实例
    auto factoryIt = intrinsic_functions.find(methodName);
    if (factoryIt == intrinsic_functions.end()) {
        String info = fmt::format("function {} not implement.", methodName);
        throw std::runtime_error(info.c_str());
    }
    auto& factory = factoryIt->second;

    for (auto& symbol : symbolSet) {
        _callables[symbol] = factory(config);
    }

    // 5. 构建输出要素
    for (auto& symbol : symbolSet) {
        String output_key = symbol + "." + _label;
        _outputs[output_key] = ArgType::Double_TimeSeries;
    }

    // 6. 从连接信息解析实际输入映射
    _resolvedInputs = resolveInputConnections();

    DEBUG_INFO("[FunctionNode:{}] Init: _resolvedInputs has {} entries, _callables has {} entries, _outputs has {} entries",
         _id, _resolvedInputs.size(), _callables.size(), _outputs.size());
    return true;
}

NodeProcessResult FunctionNode::Process(const String& strategy, DataContext& context)
{
    if (_callables.empty()) {
        WARN("[FunctionNode:{}] No callables initialized", _id);
        return NodeProcessResult::Error;
    }

    // 对每个 symbol 独立计算
    for (auto& item : _callables) {
        auto& symbol = item.first;
        // 构建该 symbol 的输入参数（从连接信息解析的映射）
        Map<String, context_t> args;
        for (auto& [slot, contextKey] : _resolvedInputs) {
            if (context.exist(contextKey)) {
                args[slot] = context.get(contextKey);
            }
        }

        if (args.empty()) {
            DEBUG_INFO("[FunctionNode:{}] No input data for symbol {}", _id, symbol);
            continue;
        }

        auto callable = item.second;
        // 调用计算
        auto result = (*callable)(args);

        // 写入输出
        String output_key = symbol + "." + _label;
        std::visit([this, &output_key, &context, &symbol](const auto& val) {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, double>) {
                if (context.exist(output_key)) {
                    context.add(output_key, val);
                } else {
                    Vector<double> ts;
                    ts.push_back(val);
                    context.set(output_key, ts);
                }
            } else if constexpr (std::is_same_v<T, Vector<double>>) {
                if (context.exist(output_key)) {
                    auto& existing = context.get<Vector<double>>(output_key);
                    for (auto v : val) existing.push_back(v);
                } else {
                    context.set(output_key, val);
                }
            } else {
                WARN("[FunctionNode:{}] Unsupported return type for symbol {}", _id, symbol);
            }
        }, result);
    }

    return NodeProcessResult::Success;
}

Map<String, ArgType> FunctionNode::out_elements() {
    return _outputs;
}

const nlohmann::json FunctionNode::getParams() {
    nlohmann::json params;
    for (auto& item: intrinsic_functions) {
        auto key = item.first;
        params[key] = {{"args", "type"}};
    }
    return params;
}
