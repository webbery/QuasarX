#include "Nodes/FunctionNode.h"
#include "Nodes/QuoteNode.h"
#include "StrategyNode.h"
#include "Util/string_algorithm.h"
#include "Util/datetime.h"
#include "Util/system.h"
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
    // 每 bar 对应的分钟数
    int MinutesPerBar(DataFrequencyType freq) {
        switch (freq) {
            case DataFrequencyType::Day:    return 240;
            case DataFrequencyType::Min1:   return 1;
            case DataFrequencyType::Min5:   return 5;
            case DataFrequencyType::Second: return 1;
            default: return 240;
        }
    }

    // 将 TimeValue 按数据频率换算为 bar 数
    int TimeValueToBars(const TimeValue& tv, DataFrequencyType freq) {
        int totalMinutes;
        switch (tv.unit) {
            case 's': totalMinutes = 1; break;             // 不足 1 bar 按 1 bar
            case 'm': totalMinutes = tv.value; break;
            case 'h': totalMinutes = tv.value * 60; break;
            case 'd': totalMinutes = tv.value * 240; break; // 1 交易日 = 240 分钟
            default:  return tv.value;                      // 无单位 → 裸数字即 bar 数
        }
        return std::max(1, totalMinutes / MinutesPerBar(freq));
    }

    using CallableFactory = ICallable* (*)(const nlohmann::json&);

    struct IntrinsicEntry {
        const char* name;
        CallableFactory factory;
    };

    static const IntrinsicEntry intrinsic_table[] = {
        {"MA", [] (const nlohmann::json& config) -> ICallable* {
            return new MA(config.value("_windowBars", 15));
        }},
        {"MinMax", [] (const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"Z-score", [] (const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"ATR", [] (const nlohmann::json& config) -> ICallable* {
            return new ATR(config.value("_windowBars", 14));
        }},
        {"VWAP", [] (const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"RSI", [] (const nlohmann::json& config) -> ICallable* {
            return nullptr;
        }},
        {"STD", [] (const nlohmann::json& config) -> ICallable* {
            return new STD(config.value("_windowBars", 15));
        }},
        {"Return", [] (const nlohmann::json& config) -> ICallable* {
            return new Return(config.value("_windowBars", 1));
        }},
        {"R2", [] (const nlohmann::json& config) -> ICallable* {
            return new R2(config.value("_windowBars", 15));
        }},
        {"ZScore", [] (const nlohmann::json& config) -> ICallable* {
            return new ZScore(config.value("_windowBars", 15));
        }},
        {"VPCorr", [] (const nlohmann::json& config) -> ICallable* {
            return new VPCorr(config.value("_windowBars", 15));
        }},
    };
    constexpr size_t intrinsic_count = sizeof(intrinsic_table) / sizeof(intrinsic_table[0]);
}

List<String> GetAllFunctionNames() {
    List<String> names;
    for (size_t i = 0; i < intrinsic_count; i++) {
        names.push_back(intrinsic_table[i].name);
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

    // 2. 获取方法名
    if (!config.contains("params") || !config["params"].contains("method")
        || !config["params"]["method"].contains("value")) {
        throw std::runtime_error(fmt::format(
            "[FunctionNode:{}] Init: missing params.method.value in config", _id));
    }
    String methodName = config["params"]["method"]["value"];

    // 2.5 从上游 input 节点提取数据频率，将 range 换算为 bar 数
    DataFrequencyType dataFreq = DataFrequencyType::Day;
    for (auto& [handle, node] : _ins) {
        if (auto* quoteNode = dynamic_cast<QuoteInputNode*>(node)) {
            dataFreq = quoteNode->GetFreq();
            break;
        }
    }
    int windowBars = 15; // 默认值
    if (config["params"].contains("range") && config["params"]["range"].contains("value")) {
        String rangeStr = (String)config["params"]["range"]["value"];
        TimeValue tv = ParseTimeValue(rangeStr);
        windowBars = TimeValueToBars(tv, dataFreq);
        DEBUG_INFO("[FunctionNode:{}] Init: range='{}', dataFreq={}, windowBars={}",
             _id, rangeStr, static_cast<int>(dataFreq), windowBars);
    }
    auto callableConfig = config;
    callableConfig["_windowBars"] = windowBars;

    // 3. 从上游 QuoteInputNode 提取 symbol 集合（BFS 遍历）
    Set<String> symbolSet;
    for (auto sym : discoverUpstreamSymbols()) {
        symbolSet.insert(get_symbol(sym));
    }
    DEBUG_INFO("[FunctionNode:{}] Init: discoverUpstreamSymbols → {} symbols",
         _id, symbolSet.size());

    // 4. 为每个 symbol 创建独立的 callable 实例
    CallableFactory factory = nullptr;
    for (size_t i = 0; i < intrinsic_count; i++) {
        if (methodName == intrinsic_table[i].name) {
            factory = intrinsic_table[i].factory;
            break;
        }
    }
    if (!factory) {
        String info = fmt::format("function {} not implement.", methodName);
        throw std::runtime_error(info.c_str());
    }

    for (auto& symbol : symbolSet) {
        auto* callable = factory(callableConfig);
        if (!callable) {
            throw std::runtime_error(fmt::format(
                "[FunctionNode:{}] method '{}' factory returned nullptr for symbol '{}'",
                _id, methodName, symbol));
        }
        _callables[symbol] = callable;
    }

    // 5. 构建输出要素
    for (auto& symbol : symbolSet) {
        String output_key = symbol + "." + _label;
        _outputs[output_key] = ArgType::Double_TimeSeries;
    }

    // 6. 从连接信息解析实际输入映射（保持原始 dataName 作为 key）
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

    // 防御性检查：确保所有 callable 非空
    for (auto& [sym, ptr] : _callables) {
        if (!ptr) {
            ERROR("[FunctionNode:{}] nullptr callable for symbol '{}'", _id, sym);
            return NodeProcessResult::Error;
        }
    }

    // 对每个 symbol 独立计算
    for (auto& item : _callables) {
        auto& symbol = item.first;
        // 构建该 symbol 的输入参数（从连接信息解析的映射）
        Map<String, context_t> args;
        for (auto& [dataName, contextKey] : _resolvedInputs) {
            String symbolKey = contextKey;
            auto firstDot = contextKey.find('.');
            if (firstDot != String::npos) {
                auto secondDot = contextKey.find('.', firstDot + 1);
                if (secondDot != String::npos) {
                    symbolKey = symbol + contextKey.substr(secondDot);
                }
            }
            if (context.exist(symbolKey)) {
                String slot = (dataName == "close") ? "price" : dataName;
                args[slot] = context.get(symbolKey);
            }
            else {
                INFO("[FunctionNode:{}] key {} not exist.", _id, symbolKey);
            }
        }

        if (args.empty()) {
            DEBUG_INFO("[FunctionNode:{}] No input data for symbol {}", _id, symbol);
            continue;
        }

        auto callable = item.second;
        INFO("[FunctionNode:{}] Process: calling {} with {} args for symbol {}",
             _id, _label, args.size(), symbol);
        auto result = (*callable)(args);
        INFO("[FunctionNode:{}] Process: callable returned, writing output '{}.{}'",
             _id, symbol, _label);

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
    for (size_t i = 0; i < intrinsic_count; i++) {
        params[intrinsic_table[i].name] = {{"args", "type"}};
    }
    return params;
}
