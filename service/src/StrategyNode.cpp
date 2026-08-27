#include "StrategyNode.h"
#include "Nodes/QuoteNode.h"
#include "Nodes/CacheFeatureNode.h"

Set<symbol_t> QNode::discoverUpstreamSymbols() {
    Set<symbol_t> symbols;
    Set<QNode*> visited;
    Vector<QNode*> queue;

    for (auto& item : _ins) {
        queue.push_back(item.second);
    }

    while (!queue.empty()) {
        QNode* current = queue.back();
        queue.pop_back();

        if (visited.count(current)) continue;
        visited.insert(current);

        if (auto* quoteNode = dynamic_cast<QuoteInputNode*>(current)) {
            for (const auto& sym : quoteNode->GetSymbols()) {
                symbols.insert(sym);
            }
        } else if (auto* cacheNode = dynamic_cast<CacheFeatureNode*>(current)) {
            for (const auto& sym : cacheNode->GetSymbols()) {
                symbols.insert(sym);
            }
        }

        for (auto& item : current->ins()) {
            if (!visited.count(item.second)) {
                queue.push_back(item.second);
            }
        }
    }

    return symbols;
}

Map<String, ArgType> QNode::out_elements() {
    Map<String, ArgType> elems;
    return elems;
}

Map<String, String> QNode::resolveInputConnections() {
    Map<String, String> dataToContext;

    for (auto& [targetHandle, upstreamNode] : _ins) {
        for (auto& [sourceHandle, downstreamNode] : upstreamNode->outs()) {
            if (downstreamNode != this) continue;

            auto outs = upstreamNode->out_elements();

            // 从 sourceHandle 提取数据名
            // "11-IMF_0" → "IMF_0", "1-close" → "close", "11-energy_velocity" → "energy_velocity"
            String dataName;
            auto dashPos = sourceHandle.find('-');
            if (dashPos != String::npos) {
                dataName = sourceHandle.substr(dashPos + 1);
            }

            if (!dataName.empty()) {
                // 有明确的数据名，从 out_elements 中匹配 context key
                // 优先精确匹配（key 以 ".{dataName}" 结尾），回退到子串匹配
                String fallbackKey;
                for (auto& [key, type] : outs) {
                    auto suffix = "." + dataName;
                    if (key.size() >= suffix.size() &&
                        key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
                        dataToContext[dataName] = key;
                        fallbackKey.clear();
                        break;
                    }
                    if (fallbackKey.empty() && key.find(dataName) != String::npos) {
                        fallbackKey = key;
                    }
                }
                if (dataToContext.find(dataName) == dataToContext.end() && !fallbackKey.empty()) {
                    dataToContext[dataName] = fallbackKey;
                }
            } else {
                // sourceHandle 仅为节点 ID，无明确数据名
                // 从上游输出 key 中提取字段名（格式: "symbol.fieldName"）
                // FunctionNode 等节点有多个输出（每个 symbol 一个），但字段名相同
                Map<String, String> fieldToContext;
                for (auto& [key, type] : outs) {
                    auto dotPos = key.rfind('.');
                    if (dotPos != String::npos) {
                        String fieldName = key.substr(dotPos + 1);
                        if (fieldToContext.find(fieldName) == fieldToContext.end()) {
                            fieldToContext[fieldName] = key;
                        }
                    }
                }
                for (auto& [fn, ck] : fieldToContext) {
                    dataToContext[fn] = ck;
                }
            }
        }
    }

    return dataToContext;
}

#if 0
String ArtificialIntelligenceNode::ConvertServerModelPath(const String& uploadPath) {
    auto pos = uploadPath.find_last_of('/');
    String model_name = uploadPath.substr(pos + 1);
    return "models/" + model_name;
}


std::vector<std::vector<int64_t>> ArtificialIntelligenceNode::InitInput() {
    size_t num_input_nodes = _session->GetInputCount();
    std::vector<std::vector<int64_t>> input_shapes;

    Ort::AllocatorWithDefaultOptions allocator;
    for (size_t i = 0; i < num_input_nodes; ++i) {
        auto name = _session->GetInputNameAllocated(i, allocator);
        _modelInputs.push_back(name.get());

        auto input_type_info = _session->GetInputTypeInfo(i);
        auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
        input_shapes.push_back(tensor_info.GetShape());  // 存储输入形状
    }
    return input_shapes;
}

std::vector<std::vector<int64_t>> ArtificialIntelligenceNode::InitOutput() {
    size_t num_output_nodes = _session->GetOutputCount();
    std::vector<std::vector<int64_t>> output_shapes;

    Ort::AllocatorWithDefaultOptions allocator;
    for (size_t i = 0; i < num_output_nodes; ++i) {
        auto name = _session->GetOutputNameAllocated(i, allocator);
        _modelOutputs.push_back(name.get());

        auto output_type_info = _session->GetOutputTypeInfo(i);
        auto tensor_info = output_type_info.GetTensorTypeAndShapeInfo();
        output_shapes.push_back(tensor_info.GetShape());  // 存储输出形状
    }
    return output_shapes;
}
#endif
