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

    /// 每种方法的槽位 → 字段映射
    static const Map<String, Map<String, String>> methodSlotMap{
        {"MA",      {{"price", "close"}}},
        {"STD",     {{"price", "close"}}},
        {"R2",      {{"price", "close"}}},
        {"ZScore",  {{"price", "close"}}},
        {"Return",  {{"price", "close"}}},
        {"VPCorr",  {{"price", "close"}, {"volume", "volume"}}},
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
            return nullptr;
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

    // 2. 获取方法名和槽位映射
    String methodName = config["params"]["method"]["value"];
    auto slotIt = methodSlotMap.find(methodName);
    if (slotIt == methodSlotMap.end()) {
        // 未知方法，使用默认单槽位
        _slot_to_field["price"] = "close";
    } else {
        _slot_to_field = slotIt->second;
    }

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

    DEBUG_INFO("[FunctionNode:{}] Init: _slot_to_field has {} entries, _callables has {} entries, _outputs has {} entries",
         _id, _slot_to_field.size(), _callables.size(), _outputs.size());
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
        // 构建该 symbol 的输入参数（按槽位名）
        Map<String, context_t> args;
        for (auto& [slot, field] : _slot_to_field) {
            String key = symbol + "." + field;
            if (context.exist(key)) {
                args[slot] = context.get(key);
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
