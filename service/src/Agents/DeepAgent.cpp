#include "Agents/DeepAgent.h"
#include <cstring>
#include <tvm/runtime/registry.h>

DeepAgent::DeepAgent(const String& path, int classes, const nlohmann::json& params)
:_modelpath(path), _params(params), _classes(classes) {
    auto module = tvm::runtime::Module::LoadFromFile(path);
    _model = (*tvm::runtime::Registry::Get("tvm.graph_executor.create"))(
        module->GetFunction("default")(),  // 默认设备
        module->GetFunction("get_graph_json")(),
        module->GetFunction("get_lib")(),
        module->GetFunction("get_params")()
    );
}

DeepAgent::~DeepAgent() {
}

int DeepAgent::predict(const DataFeatures& data, Vector<float>& result) {
    assert(data._dimesion.size() > 0);

    DLTensor input_tensor;
    input_tensor.data = (void*)&(data._data[0]);
    input_tensor.device = DLDevice{kDLCPU, 0};
    input_tensor.ndim = data._dimesion.size();
    input_tensor.dtype = DLDataType{kDLFloat, 32, 1};
    input_tensor.shape = (int64_t*)&(data._dimesion[0]);
    input_tensor.strides = nullptr;
    input_tensor.byte_offset = 0;

    tvm::runtime::PackedFunc set_input = _model.GetFunction("set_input");
    set_input("data", &input_tensor);

    tvm::runtime::PackedFunc run = _model.GetFunction("run");
    run();

    tvm::runtime::PackedFunc get_output = _model.GetFunction("get_output");
    DLTensor output_tensor;
    get_output(0, &output_tensor);

    float* output_data = static_cast<float*>(output_tensor.data);
    result.resize(output_tensor.ndim);
    memcpy(&(result[0]), output_data, output_tensor.ndim);
    return 0;
}

void DeepAgent::train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch) {

}
