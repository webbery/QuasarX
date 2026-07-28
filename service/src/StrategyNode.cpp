#include "StrategyNode.h"


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
                for (auto& [key, type] : outs) {
                    if (key.find(dataName) != String::npos) {
                        dataToContext[dataName] = key;
                        break;
                    }
                }
            } else {
                // sourceHandle 仅为节点 ID，无明确数据名
                // 如果上游只有一个输出，直接使用
                if (outs.size() == 1) {
                    auto& [key, type] = *outs.begin();
                    String fieldName;
                    auto dotPos = key.rfind('.');
                    if (dotPos != String::npos) {
                        fieldName = key.substr(dotPos + 1);
                    }
                    if (!fieldName.empty()) {
                        dataToContext[fieldName] = key;
                    }
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
