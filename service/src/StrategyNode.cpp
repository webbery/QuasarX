#include "StrategyNode.h"


Map<String, ArgType> QNode::out_elements() {
    Map<String, ArgType> elems;
    return elems;
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
