#include "Nodes/TestNode.h"
#include "DataContext.h"
#include "Util/datetime.h"
#include "Util/log.h"

TestNode::TestNode(Server* server)
{
    _pR2 = new R2(10);
}

TestNode::~TestNode() {
    delete _pR2;
}

const nlohmann::json TestNode::getParams() {
    nlohmann::json params;
    return params;
}

bool TestNode::Init(const nlohmann::json& config) {
    String info = config.dump();
    for (auto& item: _ins) {
        auto& in_name = item.first;
        auto in_node = item.second;
        auto outs = in_node->out_elements();
        for (auto& out: outs) {
            INFO("Out {}", out.first);
            _input_keys.insert(out.first);
        }
    }
    return true;
}

bool TestNode::Process(const String& strategy, DataContext& context) {
    // 计算R^2指标，sz.000001.close
    for (auto& key: _input_keys) {
        auto& closes = context.get(key);
        Map<String, context_t> arg;
        arg[key] = closes;
        auto r2 = (*_pR2)(arg);
        if (context.exist(key)) {[[likely]]
            context.add("r2", std::get<double>(r2));
        } else {
            Vector<double> timeseriel;
            timeseriel.push_back(std::get<double>(r2));
            context.add("r2", timeseriel);
        }
    }
    return true;
}