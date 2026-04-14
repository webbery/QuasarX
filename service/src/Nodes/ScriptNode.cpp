#include "Nodes/ScriptNode.h"

namespace {

}


bool ScriptNode::Init(const nlohmann::json& config) {
    return true;
}
NodeProcessResult ScriptNode::Process(const String& strategy, DataContext& context) {
    return NodeProcessResult::Success;
}

const nlohmann::json ScriptNode::getParams() {

    return {};
}