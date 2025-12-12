#include "Nodes/DebugNode.h"
#include "server.h"

DebugNode::DebugNode(Server* server)
:_server(server) {

}

bool DebugNode::Init(const nlohmann::json& config) {
    _suffix = (String)config["params"]["suffix"]["value"];
    // 读取输入节点的输出
    for (auto node: _ins) {

    }
    return true;
}

bool DebugNode::Process(const String& strategy, DataContext& context) {
    for (auto& name: _inNames) {
        auto& feature = context.get(name);
    }
    return true;
}

void DebugNode::Done(const String& strategy) {
    // 保存数据到/data/debug/strategy路径以便下载
    auto& cfg = _server->GetConfig();
    auto datapath = cfg.GetDatabasePath() + "/data/debug/" + strategy + _suffix;

}
const nlohmann::json DebugNode::getParams() {
    return {};
}