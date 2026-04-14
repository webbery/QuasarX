#include "Interprecter/Stmt.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "peglib.h"
#include "server.h"
#include <cstdint>
#include <functional>
#include <variant>
#include <stack>
#include <queue>
#include <algorithm>
#include <cmath>

#define INTRINSIC_TOPK      "topk"
#define INTRINSIC_BOTTOMK   "bottomk"
#define INTRINSIC_RANK      "rank"
#define INTRINSIC_ZSCORE    "zscore"
#define INTRINSIC_PERCENTILE "pct"

// 声明命名空间中的外部符号
extern bool check_bool(const context_t& feature);

// ========== FormulaParser 核心方法实现 ==========

String FormulaParser::cleanInputString(const String& input) {
    String result;
    for (char c : input) {
        if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r') {
            result += c;
        }
    }
    return result;
}

FormulaParser::FormulaParser(Server* server): _server(server), _default(TradeAction::HOLD) {
    _parser.set_logger([](size_t line, size_t col, const std::string& msg) {
        auto info = fmt::format("{} {}: {}", line, col, msg);
        strategy_error("", info);
        INFO("parse fail: {}", info);
    });
    _parser.enable_packrat_parsing();
    if (!_parser.load_grammar(statement::grammar)) {
        return ;
    }
    _parser.enable_ast();
}

bool FormulaParser::parse(const String& code) {
    _codes = cleanInputString(code);
    INFO("Parsing: '{}'", _codes);
    if (_parser.parse(_codes, _ast)) {
        // printAST(_ast);
        _ast = _parser.optimize_ast(_ast);

#ifdef _DEBUG
        INFO("** Node: {}, token: {}", _ast->name, _ast->token);
        printAST(_ast);
#endif
        return true;
    } else {
        FATAL("Parse failed for formula: {}", _codes);
        return false;
    }
}

void FormulaParser::printAST(std::shared_ptr<peg::Ast> ast, int lvl ) {
    for (auto& node : ast->nodes) {
        String tabs("  ");
        for (int i = 0; i < lvl; ++i) {
            tabs += "  ";
        }
        INFO("{}Node: {}, token: {}", tabs, node->name, node->token);
        printAST(node, ++lvl);
    }
}

bool FormulaParser::parse(const String& code, TradeAction action) {
    _default = action;
    return parse(code);
}

CrossSectionFuncType FormulaParser::getFuncType(const String& name) {
    static const Map<String, CrossSectionFuncType> typeMap{
        {INTRINSIC_TOPK, CrossSectionFuncType::TOPK},
        {INTRINSIC_BOTTOMK, CrossSectionFuncType::BOTTOMK},
        {INTRINSIC_RANK, CrossSectionFuncType::RANK},
        {INTRINSIC_ZSCORE, CrossSectionFuncType::ZSCORE},
        {INTRINSIC_PERCENTILE, CrossSectionFuncType::PERCENTILE}
    };

    auto it = typeMap.find(name);
    return (it != typeMap.end()) ? it->second : CrossSectionFuncType::RAW;
}

bool FormulaParser::isCrossSectionFunction(const String& funName) {
    static const UnorderedSet<String> crossFuncs{
        INTRINSIC_TOPK,
        INTRINSIC_BOTTOMK,
        INTRINSIC_RANK,
        INTRINSIC_ZSCORE,
        INTRINSIC_PERCENTILE,
    };
    return crossFuncs.count(funName);
}

bool FormulaParser::hasCrossSectionFunctions(const peg::Ast& ast) {
    std::stack<const peg::Ast*> stack;
    stack.push(&ast);

    while (!stack.empty()) {
        const peg::Ast* current = stack.top();
        stack.pop();

        if (current->name == "FunctionCall") {
            if (!current->nodes.empty()) {
                auto& firstChild = current->nodes[0];
                if (firstChild->name == "Identifier") {
                    String funcName(firstChild->token);
                    if (isCrossSectionFunction(funcName)) {
                        return true;
                    }
                }
            }
        }

        for (auto it = current->nodes.rbegin(); it != current->nodes.rend(); ++it) {
            stack.push(it->get());
        }
    }
    return false;
}

// 从表达式提取截面函数并建图（递归）
std::string FormulaParser::extractAndBuildGraph(const peg::Ast& node, int& counter) {
    if (node.name == "FunctionCall") {
        String funcName(node.nodes[0]->token);
        if (!isCrossSectionFunction(funcName)) {
            // 不是截面函数，递归处理子节点
            for (auto& child : node.nodes) {
                extractAndBuildGraph(*child, counter);
            }
            return "";
        }

        // 创建截面节点
        String nodeId = "__cs_" + std::to_string(counter++) + "__";
        auto& csNode = _csGraph.addNode(nodeId);
        csNode.name = funcName;
        csNode.type = getFuncType(funcName);

        // 解析参数
        if (node.nodes.size() > 1) {
            auto& argsNode = node.nodes[1];  // Arguments
            if (argsNode->name == "Arguments" && !argsNode->nodes.empty()) {
                // 第一个参数是表达式
                auto& firstArg = argsNode->nodes[0];
                String innerDep = extractAndBuildGraph(*firstArg, counter);
                if (!innerDep.empty()) {
                    csNode.dependencies.push_back(innerDep);
                    _csGraph.addEdge(nodeId, innerDep);
                }
                csNode.exprAst = firstArg;

                // 第二个参数（如果有，如 topk 的 k 值）
                if (argsNode->nodes.size() > 1) {
                    auto& secondArg = argsNode->nodes[1];
                    if (secondArg->name == "Number") {
                        csNode.param = secondArg->token_to_number<double>();
                    }
                }
            }
        }

        // 注册变量名到节点 ID 的映射
        _varToNodeId[funcName] = nodeId;
        return nodeId;
    }
    else {
        // 非函数调用，递归处理子节点
        for (auto& child : node.nodes) {
            extractAndBuildGraph(*child, counter);
        }
        return "";
    }
}

// 从 AST 构建图
void FormulaParser::buildCrossSectionGraph(const peg::Ast& ast) {
    _csGraph.clear();
    _varToNodeId.clear();

    int nodeCounter = 0;
    extractAndBuildGraph(ast, nodeCounter);

    // 拓扑排序
    if (!_csGraph.topologicalSort()) {
        WARN("CrossSectionGraph has cycle!");
    }
}

// 计算单个节点
void FormulaParser::computeNode(CrossSectionNode& node, const Vector<symbol_t>& symbols, DataContext& context) {
    if (node.computed) return;

    // 先计算所有依赖
    for (auto& depId : node.dependencies) {
        auto it = _csGraph.nodes.find(depId);
        if (it != _csGraph.nodes.end()) {
            computeNode(it->second, symbols, context);
        }
    }

    // 收集依赖节点的结果（用于嵌套函数）
    Map<symbol_t, double> depValues;
    for (auto& depId : node.dependencies) {
        auto it = _csGraph.nodes.find(depId);
        if (it != _csGraph.nodes.end() && it->second.computed) {
            for (auto& [sym, val] : it->second.outputs) {
                if (std::holds_alternative<double>(val)) {
                    depValues[sym] = std::get<double>(val);
                }
            }
        }
    }

    // 为每个 symbol 计算表达式值
    Vector<std::pair<symbol_t, double>> scores;
    for (auto symbol : symbols) {
        double score = 0.0;

        switch (node.type) {
        case CrossSectionFuncType::RAW:
        case CrossSectionFuncType::TOPK:
        case CrossSectionFuncType::BOTTOMK:
            // 计算表达式值
            {
                context_t val = evalNode(symbol, *node.exprAst, context);
                if (std::holds_alternative<double>(val)) {
                    score = std::get<double>(val);
                }
            }
            break;

        case CrossSectionFuncType::RANK:
        case CrossSectionFuncType::ZSCORE:
        case CrossSectionFuncType::PERCENTILE:
            // 使用内层函数的结果
            if (depValues.count(symbol)) {
                score = depValues[symbol];
            }
            break;
        }

        scores.emplace_back(symbol, score);
    }

    // 根据函数类型计算输出
    switch (node.type) {
    case CrossSectionFuncType::TOPK: {
        int k = 10; // 默认值
        if (std::holds_alternative<double>(node.param)) {
            k = static_cast<int>(std::get<double>(node.param));
        }
        k = std::max(1, std::min(k, static_cast<int>(scores.size())));

        // 部分排序选前 k
        std::partial_sort(scores.begin(), scores.begin() + k, scores.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        Set<symbol_t> topKSymbols;
        for (int i = 0; i < k; ++i) {
            topKSymbols.insert(scores[i].first);
        }

        for (auto symbol : symbols) {
            node.outputs[symbol] = (topKSymbols.count(symbol) > 0);
        }
        break;
    }

    case CrossSectionFuncType::BOTTOMK: {
        int k = 10; // 默认值
        if (std::holds_alternative<double>(node.param)) {
            k = static_cast<int>(std::get<double>(node.param));
        }
        k = std::max(1, std::min(k, static_cast<int>(scores.size())));

        // 部分排序选后 k（从小到大排序）
        std::partial_sort(scores.begin(), scores.begin() + k, scores.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

        Set<symbol_t> bottomKSymbols;
        for (int i = 0; i < k; ++i) {
            bottomKSymbols.insert(scores[i].first);
        }

        for (auto symbol : symbols) {
            node.outputs[symbol] = (bottomKSymbols.count(symbol) > 0);
        }
        break;
    }

    case CrossSectionFuncType::RANK: {
        // 排序后计算排名 (0~1)
        std::sort(scores.begin(), scores.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        int n = scores.size();
        for (int i = 0; i < n; ++i) {
            double rank = (n > 1) ? (static_cast<double>(i) / (n - 1)) : 0.5;
            node.outputs[scores[i].first] = rank;
        }
        break;
    }

    case CrossSectionFuncType::ZSCORE: {
        // 计算均值和标准差
        double sum = 0.0;
        for (auto& [sym, score] : scores) {
            sum += score;
        }
        double mean = (scores.empty()) ? 0.0 : (sum / scores.size());

        double sq_sum = 0.0;
        for (auto& [sym, score] : scores) {
            sq_sum += (score - mean) * (score - mean);
        }
        double std = (scores.empty()) ? 1.0 : std::sqrt(sq_sum / scores.size());

        // 标准化
        for (auto& [sym, score] : scores) {
            node.outputs[sym] = (std > 1e-10) ? ((score - mean) / std) : 0.0;
        }
        break;
    }

    case CrossSectionFuncType::PERCENTILE: {
        // 暂未实现，保持 RAW 输出
        for (auto& [sym, score] : scores) {
            node.outputs[sym] = score;
        }
        break;
    }

    default:
    case CrossSectionFuncType::RAW: {
        // RAW：直接输出分数
        for (auto& [sym, score] : scores) {
            node.outputs[sym] = score;
        }
        break;
    }
    }

    node.computed = true;
}

// 执行整个图
void FormulaParser::computeCrossSectionGraph(const Vector<symbol_t>& symbols, DataContext& context) {
    // 按拓扑序计算所有节点
    for (auto& nodeId : _csGraph.evalOrder) {
        auto& node = _csGraph.nodes.at(nodeId);
        computeNode(node, symbols, context);
    }
}

void FormulaParser::precomputeCrossSectionFunctions(const Vector<symbol_t>& symbols, DataContext& context) {
    for (const auto& [varName, func] : _CSFunctions) {
        if (func._name == INTRINSIC_TOPK) {
            auto arg1 = std::get<String>(func._args.at(0));
            Map<double, symbol_t> scores;
            for (auto symbol: symbols) {
                String key = arg1 + "." + get_symbol(symbol);
                auto& vec = context.get<Vector<double>>(key);
                scores[vec.back()] = symbol;
            }
        }
    }
}

context_t FormulaParser::evaluateForSymbolWithCrossSectionResults(const symbol_t& symbol, const peg::Ast& ast, DataContext& context,
        const Map<String, std::shared_ptr<CrossSectionResult>>& crossSectionResults) {
    return true;
}

void FormulaParser::extractCrossSectionFunctions(const peg::Ast& ast) {
    std::function<void(const peg::Ast&)> traverse = [&](const peg::Ast& node) {
        if (node.name == "FunctionCall") {
            String funcName(node.nodes[0]->token);
            if (!isCrossSectionFunction(funcName))
                return;

            auto& func = _CSFunctions[funcName];
            func._name = funcName;
            for (size_t i = 1; i < node.nodes.size(); ++i) {
                auto arguments = node.nodes[i];
                for (size_t loc = 0; loc < arguments->nodes.size(); ++loc) {
                    auto arg = arguments->nodes[loc];
                    if (arg->name == "Number") {
                        func._args[loc] = arg->token_to_number<double>();
                    }
                    else if (arg->name == "Primary") {
                        func._args[loc] = genPrimaryPlaceHolder(*arg);
                    }
                }
            }
        }

        for (auto& child : node.nodes) {
            traverse(*child);
        }
    };

    if (_CSFunctions.empty()) {
        traverse(ast);
    }
}

String FormulaParser::genPrimaryPlaceHolder(const peg::Ast& ats) {
    String code;
    return code;
}

void FormulaParser::topk(const Vector<symbol_t>& allSymbols, const peg::Ast& funcAst, CrossSectionResult& result, DataContext& context) {
    auto& args = funcAst.nodes[1];

    if (args->nodes.size() != 2) {
        WARN("topk requires 2 arguments");
        return;
    }

    auto& scoreExprAst = args->nodes[0];
    auto& kExprAst = args->nodes[1];

    context_t kValue = evalNode(symbol_t{}, *kExprAst, context);
    int k = 10;
    if (std::holds_alternative<double>(kValue)) {
        k = static_cast<int>(std::get<double>(kValue));
    }

    Vector<std::pair<symbol_t, double>> scores;
    for (auto symbol : allSymbols) {
        // 暂未实现
    }

    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

    Set<symbol_t> topKSymbols;
    for (int i = 0; i < std::min(k, static_cast<int>(scores.size())); ++i) {
        topKSymbols.insert(scores[i].first);
    }

    for (auto symbol : allSymbols) {
        result.stockResults[symbol] = (topKSymbols.count(symbol) > 0);
    }
}

List<Pair<symbol_t, TradeAction>> FormulaParser::envoke(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context) {
    List<Pair<symbol_t, TradeAction>> decisions;
    if (hasCrossSectionFunctions(*_ast)) {
        return envokeMixedCase(symbols, variantNames, context);
    } else {
        for (auto symbol: symbols) {
            auto exprValue = eval(symbol, *_ast, context);
            Pair<symbol_t, TradeAction> action{
                symbol, (std::get<bool>(exprValue)? _default: TradeAction::HOLD)
            };
            decisions.emplace_back(std::move(action));
        }
    }
    return decisions;
}

List<Pair<symbol_t, TradeAction>> FormulaParser::envokeMixedCase(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context) {
    List<Pair<symbol_t, TradeAction>> decisions;

    // Step 1: 从 AST 构建图
    buildCrossSectionGraph(*_ast);

    // Step 2: 执行图计算
    computeCrossSectionGraph(symbols, context);

    // Step 3: 为每个 symbol 求值
    for (auto symbol : symbols) {
        context_t exprValue = evalNode(symbol, *_ast, context);
        TradeAction action = (statement::check_bool(exprValue) ? _default : TradeAction::HOLD);
        decisions.emplace_back(symbol, action);
    }

    return decisions;
}
