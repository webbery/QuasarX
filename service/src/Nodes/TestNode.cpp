#include "Nodes/TestNode.h"

TestNode::TestNode(Server* server)
{
}

TestNode::~TestNode() {

}

const nlohmann::json TestNode::getParams() {
    nlohmann::json params;
    return params;
}

bool TestNode::Init(const nlohmann::json& config) {
    return true;
}

bool TestNode::Process(const String& strategy, DataContext& context) {
    return true;
}