#include "Nodes/FormulaNode.h"
#include "Interprecter/Stmt.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "server.h"

const nlohmann::json FormulaNode::getParams() {
    return {
        {"expression", "A * 0.5 + B * 0.3 + C * 0.2"}
    };
}

FormulaNode::FormulaNode(Server* server) : _server(server), _parser(nullptr) {}

FormulaNode::~FormulaNode() {
    if (_parser) delete _parser;
}

bool FormulaNode::Init(const nlohmann::json& config) {
    _label = config.value("label", "Formula");
    _expression = config["params"]["expression"]["value"].get<String>();

    _parser = new FormulaParser(_server);
    if (!_parser->parse(_expression)) {
        WARN("FormulaNode: failed to parse expression: {}", _expression);
        return false;
    }

    // 收集所有 symbol
    Set<String> loggedPrefixes;
    for (auto& item : _ins) {
        auto outs = item.second->out_elements();
        for (auto& [key, type] : outs) {
            Vector<String> tokens;
            split(key, tokens, ".");
            if (tokens.size() >= 2) {
                // 排查用：每个前缀只打一条，确认上游 key 是否形如 sh.600000.close
                if (loggedPrefixes.insert(tokens[0]).second) {
                    WARN("[FormulaNode:{}] upstream key '{}' -> to_symbol('{}')",
                         _id, key, tokens[0] + "." + tokens[1]);
                }
                symbol_t sym = to_symbol(tokens[0] + "." + tokens[1]);
                if (std::find(_symbols.begin(), _symbols.end(), sym) == _symbols.end()) {
                    _symbols.push_back(sym);
                }
            }
        }
    }

    CollectVariants();
    return true;
}

void FormulaNode::CollectVariants() {
    for (auto& item : _ins) {
        auto outs = item.second->out_elements();
        for (auto& [key, type] : outs) {
            Vector<String> tokens;
            split(key, tokens, ".");
            if (!tokens.empty()) {
                _variants.insert(tokens.back());
            }
        }
    }
}

NodeProcessResult FormulaNode::Process(const String& strategy, DataContext& context) {
    if (!_parser || _symbols.empty()) {
        WARN("[FormulaNode:{}] Process Skip: parser={}, symbols={}", _id, (bool)_parser, _symbols.size());
        return NodeProcessResult::Skip;
    }

    // 复用 FormulaParser 计算
    auto results = _parser->computeNumeric(_symbols, _variants, context);

    // 写入 DataContext
    for (auto& [sym, value] : results) {
        String key = get_symbol(sym) + "." + _label;
        if (context.exist(key)) {
            context.add(key, value);
        } else {
            Vector<double> ts;
            ts.push_back(value);
            context.set(key, ts);
        }
    }

    return NodeProcessResult::Success;
}

Map<String, ArgType> FormulaNode::out_elements() {
    Map<String, ArgType> elems;
    for (auto& sym : _symbols) {
        elems[get_symbol(sym) + "." + _label] = ArgType::Double_TimeSeries;
    }
    return elems;
}
