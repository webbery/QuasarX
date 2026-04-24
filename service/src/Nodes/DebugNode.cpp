#include "Nodes/DebugNode.h"
#include "server.h"
#include <filesystem>
#include <fstream>
#include <variant>
#include "Util/string_algorithm.h"

DebugNode::DebugNode(Server* server)
:_server(server), _context(nullptr) {

}

bool DebugNode::Init(const nlohmann::json& config) {
    _suffix = to_lower((String)config["params"]["suffix"]["value"]);
    _label = (String)config["label"];
    // 读取输入节点的输出
    for (auto node: _ins) {
        auto names = node.second->out_elements();
        for (auto& name: names) {
            _inNames.insert(name.first);
        }
    }
    return true;
}

NodeProcessResult DebugNode::Process(const String& strategy, DataContext& context) {
    _context = &context;

    return NodeProcessResult::Success;
}

void DebugNode::Done(const String& strategy) {
    if (!_context)
        return;
    
    // 保存数据到/data/debug/strategy 路径以便下载
    auto& cfg = _server->GetConfig();
    auto dir = cfg.GetDatabasePath() + "/data/debug/" + strategy;
    std::filesystem::create_directories(dir);
    auto datapath = dir + "/" + _label;
    auto& times = _context->GetTime();
    DataFrame df;
    Vector<uint32_t> indexes(times.size());
    std::iota(indexes.begin(), indexes.end(), 1);
    df.load_index(std::move(indexes));
    auto ts = Vector<time_t>(times.begin(), times.end());
    df.load_column("datetime", std::move(ts));
    for (auto& name: _inNames) {
        if (!_context->exist(name)) {
            WARN("DebugNode: key {} not found in context, skipping", name);
            continue;
        }
        auto& feature = _context->get(name);
        INFO("read colunm {}", name);
        std::visit([&name, &df](auto&& val) {
            using T = std::decay_t<decltype(val)>; // 移除引用和 cv
            if constexpr (std::is_same_v<T, double>) {
                // double 标量值，直接添加到 DataFrame
                df.load_column(name.c_str(), Vector<double>{val});
                // INFO("DebugNode::Done - collected double value: {}", val);
            }
            else if constexpr (std::is_same_v<T, Vector<float>>) {
                df.load_column(name.c_str(), val);
            }
            else if constexpr (std::is_same_v<T, Vector<double>>) {
                df.load_column(name.c_str(), val);
            } else {
                INFO("DebugNode::Done");
            }
        }, feature);
    }
    if (_suffix == "csv") {
        String save_path = dir + "/" + _label + ".csv";
        df.write<time_t, double>(save_path.c_str(), hmdf::io_format::csv2);
    }
}


const nlohmann::json DebugNode::getParams() {
    return {};
}
