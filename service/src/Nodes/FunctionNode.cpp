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
        {"Median", [] (const nlohmann::json& config) -> ICallable* {
            return new Median(config.value("_windowBars", 20));
        }},
        {"CumMax", [] (const nlohmann::json& config) -> ICallable* {
            return new CumMax(config.value("_windowBars", 252));
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

    // 2.5 解析窗口长度。
    // 上游频率只在兼容旧的 "Nd" 写法时才需要；纯数字写法与频率无关，
    // 因此同一份配置挂在日线分支就是 N 日、挂在周频分支就是 N 周。
    for (auto& [handle, node] : _ins) {
        if (auto* quoteNode = dynamic_cast<QuoteInputNode*>(node)) {
            // 视图频率沿用「第一个上游 input 节点」的口径（与迁移前一致）；
            // 但定稿计数要把所有上游 input 节点都收进来，多输入时取最大。
            if (_upstreamQuotes.empty()) {
                _viewFreq = quoteNode->GetViewFreq();
            }
            _upstreamQuotes.push_back(quoteNode);
        }
    }
    int windowBars = 15; // 默认值
    // CumMax 用于算回撤时，15 根 bar 的区间最高价没有意义（那不是回撤）。
    // 未显式配置 range 时给它一年的窗口——但只在真的没配 range 时生效，
    // 配置了 range 的一律以 range 为准。
    if (methodName == "CumMax" &&
        !(config["params"].contains("range") && config["params"]["range"].contains("value"))) {
        windowBars = 252;
        INFO("[FunctionNode:{}] CumMax without range, defaulting to 252 bars", _id);
    }
    if (config["params"].contains("range") && config["params"]["range"].contains("value")) {
        // range 单位是 bar 数，前端 type:'number' 保证序列化为 JSON 数字。
        // 非数字值视为配置错误，直接抛异常而非静默兼容。
        const auto& rv = config["params"]["range"]["value"];
        if (!rv.is_number()) {
            throw std::runtime_error(fmt::format(
                "[FunctionNode:{}] range must be a number (bar count), got: {}",
                _id, rv.type_name()));
        }
        int bars = rv.is_number_integer() ? static_cast<int>(rv.get<long long>())
                                          : static_cast<int>(rv.get<double>());
        windowBars = std::max(1, bars);
        DEBUG_INFO("[FunctionNode:{}] Init: range={}, windowBars={}", _id, bars, windowBars);
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
            FATAL("[FunctionNode:{}] nullptr callable for symbol '{}'", _id, sym);
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
            // warmup 期输入 key 尚不存在，写 NaN 占位保持输出长度同步
            String output_key = symbol + "." + _label;
            double nan = std::numeric_limits<double>::quiet_NaN();
            if (context.exist(output_key)) {
                context.add(output_key, nan);
            } else {
                context.set(output_key, Vector<double>{nan});
            }
            continue;
        }

        // 降频视图（如 freq=1w）：只有出现新的定稿 bar 时才调用 callable，
        // 让窗口按周推进；周内前向填充上一次的输出。
        //
        // 判据必须是上游 input 节点的定稿计数，不能用输入向量长度：视图序列
        // 在日线网格上逐日前向填充，长度每个 epoch 都在增长，用长度判断会让
        // MA 等窗口指标在周内逐日滑动（窗口内容随重复值滚动而改变）。
        String output_key = symbol + "." + _label;
        if (_viewFreq != DataFrequencyType::Day && !_upstreamQuotes.empty()) {
            auto sym = to_symbol(symbol);
            uint64_t version = 0;
            for (auto* quoteNode : _upstreamQuotes) {
                version = std::max(version, quoteNode->GetViewVersion(sym));
            }
            uint64_t& lastVersion = _lastViewVersion[symbol];
            if (version == lastVersion) {
                // 周内无新定稿，前向填充上一次的输出
                if (context.exist(output_key)) {
                    auto& existing = context.get<Vector<double>>(output_key);
                    if (!existing.empty()) {
                        double lastVal = existing.back();
                        context.add(output_key, lastVal);
                    } else {
                        context.add(output_key, std::numeric_limits<double>::quiet_NaN());
                    }
                } else {
                    context.set(output_key, Vector<double>{std::numeric_limits<double>::quiet_NaN()});
                }
                continue;
            }
            lastVersion = version;
        }

        // 输入已更新，正常调用 callable
        auto callable = item.second;
        auto result = (*callable)(args);

        // 写入输出
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
                if (val.empty()) {
                    // callable 返回空向量（窗口不足），写 NaN 占位
                    double nan = std::numeric_limits<double>::quiet_NaN();
                    if (context.exist(output_key)) {
                        context.add(output_key, nan);
                    } else {
                        context.set(output_key, Vector<double>{nan});
                    }
                } else if (context.exist(output_key)) {
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
