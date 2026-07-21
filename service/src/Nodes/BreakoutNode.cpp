#include "Nodes/BreakoutNode.h"
#include "server.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "boost/algorithm/string/join.hpp"

BreakoutNode::BreakoutNode(Server* server)
    : _server(server) {}

const nlohmann::json BreakoutNode::getParams() {
    return nlohmann::json::object();
}

bool BreakoutNode::Init(const nlohmann::json& config) {
    _label = (String)config["label"];

    // 遍历 _ins，按 handle 名分类收集上游输出 key
    // 前端 handle ID 格式: "input-{slot}" → 后端 _ins key = "input-{slot}"
    Map<String, Vector<String>> handleKeys;
    Set<String> allSymbols;

    for (auto& [handle, node] : _ins) {
        auto outs = node->out_elements();
        for (auto& [key, type] : outs) {
            handleKeys[handle].push_back(key);

            // 从 key 提取 symbol: "{symbol}.{field}" → symbol
            Vector<String> tokens;
            split(key, tokens, ".");
            if (tokens.size() >= 2) {
                tokens.pop_back();
                String sym = boost::algorithm::join(tokens, ".");
                allSymbols.insert(sym);
            }
        }
    }

    // 映射 handle → 第一个 context key
    auto findKey = [&](const String& handle) -> String {
        auto it = handleKeys.find(handle);
        if (it != handleKeys.end() && !it->second.empty()) {
            return it->second[0];
        }
        return "";
    };

    _valueKey = findKey("input-value");
    _upperKey = findKey("input-upper");
    _lowerKey = findKey("input-lower");

    if (_valueKey.empty() || _upperKey.empty() || _lowerKey.empty()) {
        WARN("[BreakoutNode:{}] Missing input handles: value={}, upper={}, lower={}",
             _id, _valueKey, _upperKey, _lowerKey);
        return false;
    }

    // 从 valueKey 推断 symbol 列表（value 来自 QuoteInput，覆盖所有标的）
    for (auto& key : handleKeys["input-value"]) {
        Vector<String> tokens;
        split(key, tokens, ".");
        if (tokens.size() >= 2) {
            tokens.pop_back();
            String sym = boost::algorithm::join(tokens, ".");
            _states[sym] = SymbolState{0, 0};
        }
    }

    // 注册输出: {symbol}.{label} 和 {symbol}.{label}_duration
    for (auto& [sym, _] : _states) {
        _outputs[sym + "." + _label] = ArgType::Double_TimeSeries;
        _outputs[sym + "." + _label + "_duration"] = ArgType::Double_TimeSeries;
    }

    INFO("[BreakoutNode:{}] Init: label='{}', {} symbols, valueKey='{}', upperKey='{}', lowerKey='{}'",
         _id, _label, _states.size(), _valueKey, _upperKey, _lowerKey);
    return true;
}

NodeProcessResult BreakoutNode::Process(const String& strategy, DataContext& context) {
    if (_states.empty()) {
        return NodeProcessResult::Skip;
    }

    for (auto& [symbol, state] : _states) {
        // 读取输入
        if (!context.exist(_valueKey) || !context.exist(_upperKey) || !context.exist(_lowerKey)) {
            continue;
        }

        const auto& valueVec = context.get<Vector<double>>(_valueKey);
        const auto& upperVec = context.get<Vector<double>>(_upperKey);
        const auto& lowerVec = context.get<Vector<double>>(_lowerKey);

        if (valueVec.empty() || upperVec.empty() || lowerVec.empty()) {
            continue;
        }

        double value = valueVec.back();
        double upper = upperVec.back();
        double lower = lowerVec.back();

        String stateKey  = symbol + "." + _label;
        String durKey    = symbol + "." + _label + "_duration";

        // NaN 保护：upper/lower 尚未就绪（上游 FormulaNode warmup）
        if (std::isnan(upper) || std::isnan(lower)) {
            if (context.exist(stateKey)) {
                context.add(stateKey, std::nan("nan"));
                context.add(durKey, std::nan("nan"));
            } else {
                Vector<double> nanVec = {std::nan("nan")};
                context.set(stateKey, nanVec);
                context.set(durKey, nanVec);
            }
            continue;
        }

        // 状态转移
        int prevState = state.state;
        int newState;

        if (value > upper) {
            newState = 1;  // ABOVE_UPPER
        } else if (value < lower) {
            newState = 3;  // BELOW_LOWER
        } else if (prevState == 1) {
            newState = 2;  // FALLBACK_UPPER: 从上轨突破区回落
        } else if (prevState == 3) {
            newState = 4;  // FALLBACK_LOWER: 从下轨突破区回落
        } else if (prevState == 2 && value > upper) {
            newState = 1;  // 从回落区重新突破上轨
        } else if (prevState == 4 && value < lower) {
            newState = 3;  // 从回落区重新突破下轨
        } else {
            newState = prevState;  // 保持
        }

        // 更新 duration
        if (newState == prevState && prevState != 0) {
            state.duration++;
        } else {
            state.duration = 1;
        }
        state.state = newState;

        // 写入 context
        if (context.exist(stateKey)) {
            context.add(stateKey, (double)newState);
            context.add(durKey, (double)state.duration);
        } else {
            context.set(stateKey, Vector<double>{(double)newState});
            context.set(durKey, Vector<double>{(double)state.duration});
        }
    }

    return NodeProcessResult::Success;
}

Map<String, ArgType> BreakoutNode::out_elements() {
    return _outputs;
}
