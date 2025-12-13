#include "Nodes/DebugNode.h"
#include "server.h"
#include <variant>

DebugNode::DebugNode(Server* server)
:_server(server), _context(nullptr) {

}

bool DebugNode::Init(const nlohmann::json& config) {
    _suffix = (String)config["params"]["suffix"]["value"];
    _label = (String)config["label"];
    // 读取输入节点的输出
    for (auto node: _ins) {
        auto names = node.second->out_elements();
        for (auto& name: names) {
            _inNames.push_back(name.first);
        }
    }
    return true;
}

bool DebugNode::Process(const String& strategy, DataContext& context) {
    _context = &context;
    
    return true;
}

void DebugNode::Done(const String& strategy) {
    // 保存数据到/data/debug/strategy路径以便下载
    auto& cfg = _server->GetConfig();
    auto datapath = cfg.GetDatabasePath() + "/data/debug/" + strategy + "/" + _label;
    for (auto& name: _inNames) {
        auto& feature = _context->get(name);
        std::visit([](auto&& val) {
            using T = decltype(val);
        }, feature);
    }
}
const nlohmann::json DebugNode::getParams() {
    return {};
}