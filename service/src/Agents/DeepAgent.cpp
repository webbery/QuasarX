#include "Agents/DeepAgent.h"
#include <tvm/runtime/registry.h>

DeepAgent::DeepAgent(const String& path, int classes, const nlohmann::json& params)
:_modelpath(path), _params(params), _classes(classes) {
    _module = tvm::runtime::Module::LoadFromFile(path);
    tvm::runtime::Module mod = (*tvm::runtime::Registry::Get("tvm.graph_executor.create"))(
        _module->GetFunction("default")(),  // 默认设备
        _module->GetFunction("get_graph_json")(),
        _module->GetFunction("get_lib")(),
        _module->GetFunction("get_params")()
    );
}

DeepAgent::~DeepAgent() {

}

int DeepAgent::classify(const DataFeatures& data, short n_samples, Vector<float>& result) {
    tvm::runtime::NDArray input_tensor = tvm::runtime::NDArray::Empty(
        {1, 10, 10}, DLDataType{kDLFloat, 32, 1}, DLDevice{kDLCPU, 0});
}

double DeepAgent::predict() {

}

void DeepAgent::train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch) {

}
