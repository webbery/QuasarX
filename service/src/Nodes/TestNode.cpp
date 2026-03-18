#include "Nodes/TestNode.h"
#include "DataContext.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "std_header.h"

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
    // 计算 R^2 指标
    for (auto& key: _input_keys) {
        auto& closes = context.get(key);
        Map<String, context_t> arg;
        arg[key] = closes;
        auto r2 = (*_pR2)(arg);
        // 将 r2 值添加到 context，键为"{symbol}.r2"（与输入 key 保持一致的前缀）
        double r2_value = std::get<double>(r2);
        if (!std::isnan(r2_value)) {
            LOG("not nan: {}", r2_value);
        }
        // 从输入 key 中提取 symbol 前缀，例如 "sz.000001.close" -> "sz.000001"
        size_t last_dot = key.find_last_of('.');
        String symbol_prefix = (last_dot != String::npos) ? key.substr(0, last_dot) : key;
        String r2_key = symbol_prefix + ".r2";
        // 检查是否已存在 r2，存在则追加，否则创建新向量
        if (context.exist(r2_key)) {
            context.add(r2_key, r2_value);
        } else {
            context.set(r2_key, Vector<double>{r2_value});
        }
    }
    return true;
}

Map<String, ArgType> TestNode::out_elements() {
    Map<String, ArgType> elems;
    // 输出元素名称与输入保持一致的前缀格式，例如 "sz.000001.r2"
    for (auto& key: _input_keys) {
        size_t last_dot = key.find_last_of('.');
        String symbol_prefix = (last_dot != String::npos) ? key.substr(0, last_dot) : key;
        elems[symbol_prefix + ".r2"] = Double;
    }
    return elems;
}
