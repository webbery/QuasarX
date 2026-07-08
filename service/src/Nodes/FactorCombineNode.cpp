#include "Nodes/FactorCombineNode.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "server.h"
#include "boost/algorithm/string.hpp"
#include <cmath>
#include <numeric>

const nlohmann::json FactorCombineNode::getParams() {
    return {
        {"method", "equal"},
        {"weights", ""}
    };
}

FactorCombineNode::FactorCombineNode(Server* server)
    : _server(server) {}

bool FactorCombineNode::Init(const nlohmann::json& config) {
    _label = config.value("label", "FactorCombine");

    // 解析参数
    if (config.contains("params")) {
        auto& p = config["params"];

        _method = p.contains("method") ? (String)p["method"]["value"] : "equal";
        boost::algorithm::to_lower(_method);

        if (_method == "custom" && p.contains("weights")) {
            String weights_str = (String)p["weights"]["value"];
            if (!weights_str.empty()) {
                Vector<String> tokens;
                boost::algorithm::split(tokens, weights_str, boost::is_any_of(","));
                for (auto& t : tokens) {
                    boost::algorithm::trim(t);
                    if (!t.empty()) {
                        try {
                            _weights.push_back(std::stod(t));
                        } catch (...) {
                            WARN("[FactorCombine] Invalid weight value: '{}'", t);
                            return false;
                        }
                    }
                }
            }
        }
    }

    // 收集所有输入 key，按 symbol 分组
    for (auto& item : _ins) {
        auto outs = item.second->out_elements();
        for (auto& [key, type] : outs) {
            // 提取 symbol：key 格式 "{symbol}.{factorName}"
            Vector<String> tokens;
            split(key, tokens, ".");
            if (tokens.size() < 2) {
                WARN("[FactorCombine] Input key '{}' has no symbol prefix, skipping", key);
                continue;
            }

            // symbol = 除最后一个 token 外的所有部分（支持 sh600519 这种两段式）
            String symbol = boost::algorithm::join(
                boost::make_iterator_range(tokens.begin(), tokens.end() - 1), ".");

            size_t idx = _factorKeys.size();
            _factorKeys.push_back(key);
            _symbolFactorIndices[symbol].push_back(idx);
        }
    }

    if (_factorKeys.empty()) {
        WARN("[FactorCombine] No input factors found");
        return false;
    }

    // 验证 custom 模式的权重数量
    if (_method == "custom" && !_weights.empty()) {
        // 权重按 symbol 分组后每组应一致（每个 symbol 有相同数量的因子）
        // 这里不强制校验，在 Process 中按实际因子数处理
        INFO("[FactorCombine] Custom weights: {} values for {} factors",
             _weights.size(), _factorKeys.size());
    }

    // 注册输出
    for (auto& [symbol, indices] : _symbolFactorIndices) {
        String outputKey = symbol + ".composite_score";
        _outputs[outputKey] = ArgType::Double_TimeSeries;
    }

    INFO("[FactorCombine] Init: method='{}', {} factors, {} symbols",
         _method, _factorKeys.size(), _symbolFactorIndices.size());

    return true;
}

NodeProcessResult FactorCombineNode::Process(const String& strategy, DataContext& context) {
    if (_factorKeys.empty()) {
        return NodeProcessResult::Skip;
    }

    for (auto& [symbol, indices] : _symbolFactorIndices) {
        // 收集当前 step 各因子的值
        Vector<double> factorValues;
        Vector<double> factorWeights;

        for (size_t i = 0; i < indices.size(); ++i) {
            size_t keyIdx = indices[i];
            const String& key = _factorKeys[keyIdx];

            if (!context.exist(key)) {
                continue;
            }

            // 读取最新值
            double value = std::nan("");
            auto& ctx = context.get(key);
            std::visit([&value](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, Vector<double>>) {
                    if (!v.empty()) value = v.back();
                } else if constexpr (std::is_same_v<T, double>) {
                    value = v;
                }
            }, ctx);

            if (std::isnan(value)) {
                continue;  // 跳过 NaN 因子
            }

            factorValues.push_back(value);

            // 确定权重
            if (_method == "custom" && keyIdx < _weights.size()) {
                factorWeights.push_back(_weights[keyIdx]);
            } else {
                factorWeights.push_back(1.0);
            }
        }

        if (factorValues.empty()) {
            continue;  // 所有因子都是 NaN，跳过该 symbol
        }

        // 计算合成得分
        double score = 0.0;
        double weightSum = 0.0;
        for (size_t i = 0; i < factorValues.size(); ++i) {
            score += factorValues[i] * factorWeights[i];
            weightSum += factorWeights[i];
        }

        if (weightSum > 0.0) {
            score /= weightSum;  // 归一化
        }

        // 写入 DataContext
        String outputKey = symbol + ".composite_score";
        if (context.exist(outputKey)) {
            context.add(outputKey, score);
        } else {
            Vector<double> ts;
            ts.push_back(score);
            context.set(outputKey, ts);
        }
    }

    return NodeProcessResult::Success;
}

Map<String, ArgType> FactorCombineNode::out_elements() {
    return _outputs;
}
