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

    Map<String, std::function<ICallable* (const FunctionNode&, const nlohmann::json&)>> intrinsic_functions{
        {"MA", [] (const FunctionNode& node, const nlohmann::json& config) {
            // range: 平滑时间参数（时间范围，格式：正整数 + s/m/h/d 后缀，如 "5d", "30m"）
            // 解析规则：提取数字部分作为 MA 周期，单位后缀仅作语义提示
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
            throw std::runtime_error("Invalid MA range format: " + range + ", expected format like '5d', '30m', '1h'");
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
        {"VPCorr", [] (const FunctionNode& node, const nlohmann::json& config) -> ICallable* {
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
    DEBUG_INFO("[FunctionNode:{}] Init: _ins size = {}", _id, _ins.size());
    for (auto& item: _ins) {
        auto input_names = item.second->out_elements();
        DEBUG_INFO("[FunctionNode:{}] Init: input node '{}' provided {} elements",
             _id, item.first, input_names.size());
        _params.merge(input_names);
    }

    // 构建输入到输出的映射，并收集所有 symbol
    _label = (String)config["label"];
    DEBUG_INFO("[FunctionNode:{}] Init: label='{}', _params size = {}",
         _id, _label, _params.size());

    // 获取方法名，判断是否需要命名槽位
    String methodName = config["params"]["method"]["value"];
    bool useNamedSlots = (methodName == "VPCorr");

    if (useNamedSlots) {
        // 命名槽位模式：按槽位名提取指定 field
        // VPCorr 需要: price(close) + volume
        // 从 _params 中筛选出匹配的 key
        Map<String, ArgType> slotParams;
        for (auto& item: _params) {
            auto& name = item.first;
            // 提取 field 名（最后一个 token）
            Vector<String> tokens;
            split(name, tokens, ".");
            if (tokens.empty()) continue;
            String field = tokens.back();

            // 根据 field 名分配到槽位
            if (field == "close" || field == "price") {
                slotParams["price"] = item.second;
            } else if (field == "volume") {
                slotParams["volume"] = item.second;
            }
        }
        _params = slotParams;

        // 为命名槽位建立输出映射
        // 取第一个 symbol 作为输出 symbol
        String outputSymbol;
        for (auto& item: _ins) {
            auto outs = item.second->out_elements();
            for (auto& [key, type] : outs) {
                Vector<String> tokens;
                split(key, tokens, ".");
                if (tokens.size() >= 2) {
                    outputSymbol = boost::algorithm::join(
                        boost::span<String>(tokens.data(), tokens.size() - 1), ".");
                    break;
                }
            }
            if (!outputSymbol.empty()) break;
        }

        if (!outputSymbol.empty()) {
            String output_key = outputSymbol + "." + _label;
            _param_to_output_map["price"] = output_key;
            _param_to_output_map["volume"] = output_key;
        }
    } else {
        // 传统模式：按 symbol 建立映射
        for (auto& item: _params) {
            auto& name = item.first;
            Vector<String> tokens;
            split(name, tokens, ".");
            if (tokens.empty()) {
                WARN("[FunctionNode:{}] Init: param '{}' has no dots, skipping",
                     _id, name);
                continue;
            }
            tokens.pop_back();  // 去掉属性名
            String symbol = boost::algorithm::join(tokens, ".");

            // 建立映射: "sh600519.close" -> "sh600519.ReturnRate"
            String output_key = symbol + "." + _label;
            _param_to_output_map[name] = output_key;
            DEBUG_INFO("[FunctionNode:{}] Init: mapped '{}' -> '{}'",
                 _id, name, output_key);
        }
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
    DEBUG_INFO("[FunctionNode:{}] Init: _param_to_output_map has {} entries, _outputs has {} entries",
         _id, _param_to_output_map.size(), _outputs.size());
    return true;
}

NodeProcessResult FunctionNode::Process(const String& strategy, DataContext& context)
{
    if (!_callable) {[[unlikely]]
        WARN("Node: function is not set");
        return NodeProcessResult::Error;
    }

    // 1. 收集输入数据
    Map<String, context_t> arguments;
    Vector<String> output_keys;

    String methodName;
    if (context.exist("_function_method")) {
        methodName = context.get<String>("_function_method");
    }

    bool useNamedSlots = (_param_to_output_map.count("price") > 0);

    if (useNamedSlots) {
        // 命名槽位模式：按槽位名提取数据
        for (auto& [slot, output_key] : _param_to_output_map) {
            if (arguments.count(slot)) continue; // 已添加
            // 从 context 中查找匹配的 key
            for (auto& [param_key, param_type] : _params) {
                if (param_key == slot) {
                    // 找到对应的 context key（需要找到实际的 symbol.field key）
                    // 遍历 context 找到匹配的 key
                    // 简化：直接用 slot 名作为 key 查找
                    if (context.exist(slot)) {
                        arguments[slot] = context.get(slot);
                    }
                }
            }
        }
        // 如果上面没找到，尝试从 _params 的原始 key 中提取
        if (arguments.empty()) {
            for (auto& [param_key, param_type] : _params) {
                Vector<String> tokens;
                split(param_key, tokens, ".");
                if (tokens.empty()) continue;
                String field = tokens.back();
                String slot;
                if (field == "close" || field == "price") slot = "price";
                else if (field == "volume") slot = "volume";
                if (!slot.empty() && context.exist(param_key)) {
                    arguments[slot] = context.get(param_key);
                }
            }
        }
    } else {
        // 传统模式
        for (auto& item: _params) {
            auto& value = context.get(item.first);
            arguments[item.first] = value;
        }
    }

    // 2. 按 _param_to_output_map 的顺序构建输出 key 列表
    Set<String> seen_outputs;
    for (auto& [input_key, output_key] : _param_to_output_map) {
        if (seen_outputs.insert(output_key).second) {
            output_keys.push_back(output_key);
        }
    }

    DEBUG_INFO("[FunctionNode:{}] Processing, label='{}', output_keys={}", 
         _id, _label, boost::algorithm::join(output_keys, ", "));

    // 3. 调用函数计算
    auto result = (*_callable)(arguments);
    
    // 4. 根据结果类型处理，按顺序写入输出
    auto ret = std::visit([this, &output_keys, &context](const auto& val) -> NodeProcessResult {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, double>) {
            // 标量：所有输出 key 都用同一个值
            for (auto& key : output_keys) {
                if (context.exist(key)) {
                    context.add(key, val);
                    DEBUG_INFO("[FunctionNode:{}] Appended value {} to existing key '{}'", _id, val, key);
                } else {
                    Vector<double> timeseries;
                    timeseries.push_back(val);
                    context.set(key, timeseries);
                    DEBUG_INFO("[FunctionNode:{}] Created new key '{}' with value {}", _id, key, val);
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
                    DEBUG_INFO("[FunctionNode:{}] Appended value {} to existing key '{}'", _id, val[i], output_keys[i]);
                } else {
                    Vector<double> timeseries;
                    timeseries.push_back(val[i]);
                    context.set(output_keys[i], timeseries);
                    DEBUG_INFO("[FunctionNode:{}] Created new key '{}' with vector[{}] = {}", _id, output_keys[i], i, val[i]);
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