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
    auto& times = _context->GetTime();

    // 收集列数据
    Vector<time_t> ts(times.begin(), times.end());
    Map<String, Vector<double>> columns;

    for (auto& name: _inNames) {
        if (!_context->exist(name)) {
            WARN("DebugNode: key {} not found in context, skipping", name);
            continue;
        }
        auto& feature = _context->get(name);
        INFO("read colunm {}", name);
        std::visit([&name, &columns](auto&& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, double>) {
                columns[name] = Vector<double>{val};
            }
            else if constexpr (std::is_same_v<T, Vector<float>>) {
                columns[name] = Vector<double>(val.begin(), val.end());
            }
            else if constexpr (std::is_same_v<T, Vector<double>>) {
                columns[name] = val;
            } else {
                INFO("DebugNode::Done");
            }
        }, feature);
    }

    if (_suffix == "csv" && !ts.empty()) {
        String save_path = dir + "/" + _label + ".csv";
        std::filesystem::create_directories(dir);

        std::ofstream ofs(save_path);
        // header
        ofs << "datetime";
        for (auto& [name, col] : columns) {
            ofs << "," << name;
        }
        ofs << "\n";
        // rows
        for (size_t i = 0; i < ts.size(); ++i) {
            ofs << ToString(ts[i], "%Y-%m-%d %H:%M:%S");
            for (auto& [name, col] : columns) {
                ofs << "," << (i < col.size() ? std::to_string(col[i]) : "");
            }
            ofs << "\n";
        }
    }
}


const nlohmann::json DebugNode::getParams() {
    return {};
}
