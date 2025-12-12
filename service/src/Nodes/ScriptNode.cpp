#include "Nodes/ScriptNode.h"

namespace {

}


bool ScriptNode::Init(const nlohmann::json& config) {
    return true;
}
bool ScriptNode::Process(const String& strategy, DataContext& context) {
    return true;
}

const nlohmann::json ScriptNode::getParams() {

    return {};
}