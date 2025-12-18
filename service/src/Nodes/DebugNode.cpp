#include "Nodes/DebugNode.h"
#include "server.h"
#include <filesystem>
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
    auto dir = cfg.GetDatabasePath() + "/data/debug/" + strategy;
    std::filesystem::create_directories(dir);
    auto datapath = dir + "/" + _label;
    auto& times = _context->GetTime();
    List<Vector<float>*> data;
    DataFrame df;
    Vector<uint32_t> indexes(times.size());
    std::iota(indexes.begin(), indexes.end(), 1);
    df.load_index(std::move(indexes));
    auto ts = Vector<time_t>(times.begin(), times.end());
    df.load_column("datetime", ts);
    for (auto& name: _inNames) {
        auto& feature = _context->get(name);
        std::visit([&data](auto&& val) {
            using T = decltype(val);
            if constexpr (std::is_same_v<T, double>) {
                
            }
            else if constexpr (std::is_same_v<T, Vector<float>>) {
                Vector<float>* p = &val;
                data.push_back(p);
            }
        }, feature);
    }
    if (_suffix == "csv") {
        SaveCSV(df, dir);
    }
}

void DebugNode::SaveCSV(const DataFrame& df, const String& dir) {
    String save_path = dir + "/" + _label + ".csv";
    df.write(save_path.c_str());
}

const nlohmann::json DebugNode::getParams() {
    return {};
}