#include "Nodes/LogisticNode.h"


const nlohmann::json LogisticNode::getParams() {
    return {};
}

bool LogisticNode::Init(const nlohmann::json& config) {
    String model_org_path = config["params"]["model"]["value"];
    String server_model_path = ConvertServerModelPath(model_org_path);

    Ort::SessionOptions session_options;
    int num_thread = std::max((int)std::thread::hardware_concurrency() / 2, 1);
    session_options.SetIntraOpNumThreads(num_thread);
    session_options.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

    _session = new Ort::Session(_env, server_model_path.c_str(), session_options);

    InitInput();
    InitOutput();
    return true;
}

bool LogisticNode::Process(const String& strategy, DataContext& context) {

    return true;
}