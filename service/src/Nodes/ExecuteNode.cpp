#include "Nodes/ExecuteNode.h"
#include "server.h"

ExecuteNode::ExecuteNode(Server* server) {

}

bool ExecuteNode::Init(const nlohmann::json& config) {
    return true;
}

bool ExecuteNode::Process(const String& strategy, DataContext& context) {

    return true;
}