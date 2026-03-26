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
#include <numeric>

#define ANY_CAST(val) any_cast<std::shared_ptr<Stmt>>(val)

#define INTRINSIC_TOPK      "topk"
#define INTRINSIC_BOTTOMK   "bottomk"
#define INTRINSIC_RANK      "rank"
#define INTRINSIC_ZSCORE    "zscore"
#define INTRINSIC_PERCENTILE "pct"

namespace  {
String grammar = R"(
        # 程序结构
        Program         <- Statement*
        Statement       <- ExpressionStmt / AssignmentStmt
        ExpressionStmt  <- Expression EOL
        AssignmentStmt  <- Identifier '=' Expression EOL

        # 表达式定义
        Expression      <- OrExpr
        OrExpr          <- AndExpr ('or' AndExpr)*
        AndExpr         <- NotExpr ('and' NotExpr)*
        NotExpr         <- 'not' NotExpr / CompareExpr
        CompareExpr     <- ArithExpr (CompareOp ArithExpr)*
        ArithExpr       <- Term (AddOp Term)*
        Term            <- Primary (MulOp Primary)*
        Primary         <- Atom (Trailer)*
        Atom            <- Number / String / FunctionCall / ListExpr / Identifier / '(' Expression ')'

        # 时间序列访问
        Trailer         <- '.' Identifier / '(' Arguments? ')' / '[' TimeOffset ']'
        TimeOffset      <- < 't' '-' [0-9]+ > / < 't' > / < [0-9]+ >

        # 函数调用
        FunctionCall    <- Identifier '(' Arguments? ')'
        Arguments       <- Expression (',' Expression)*

        # 数据结构
        ListExpr        <- '[' Expression (',' Expression)* ']'

        # 标识符和数字
        Identifier      <- < [a-zA-Z_][a-zA-Z_0-9]* >
        Number          <- < '-'? [0-9]+ ('.' [0-9-9]+)? >
        String          <- < '"' [^"]* '"' > / < "'" [^']* "'" >

        # 运算符定义
        CompareOp       <- '<=' / '>=' / '==' / '!=' / '<' / '>'
        AddOp           <- '+' / '-'
        MulOp           <- '*' / '@' / '/' / '//' / '%'

        # 语句分隔符
        EOL             <- ';' [ \t\r\n]* / !.
        %whitespace     <- [ \t]*
    )";

Map<String, std::function<bool (const context_t& , const context_t& )>> comparationMap{
    {">", [](const context_t& left, const context_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = l > std::get<double>(right);
            } else {
                INFO("not support operation `>` for type {}", typeid(T).name());
            }
        }, left);
        return val;
    }},
    {"<", [](const context_t& left, const context_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = l < std::get<double>(right);
            } else {
                INFO("not support operation `<` for type {}", typeid(T).name());
            }
        }, left);
        return val;
    }},
    {"==", [](const context_t& left, const context_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = (l == std::get<double>(right));
            } else {
                INFO("not support operation `==` for type {}", typeid(T).name());
            }
        }, left);
        return val;
    }},
    {"!=", [](const context_t& left, const context_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = (l != std::get<double>(right));
            } else {
                INFO("not support operation `!=` for type {}", typeid(T).name());
            }
        }, left);
        return val;
    }},
    {">=", [](const context_t& left, const context_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = (l >= std::get<double>(right));
            } else {
                INFO("not support operation `>=` for type {}", typeid(T).name());
            }
        }, left);
        return val;
    }},
    {"<=", [](const context_t& left, const context_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = (l <= std::get<double>(right));
            } else {
                INFO("not support operation `<=` for type {}", typeid(T).name());
            }
        }, left);
        return val;
    }},
};

Map<char, std::function<context_t(const context_t& , const context_t&)>> arithmeticMap{
    {'+', [](const context_t& left, const context_t& right) {
        context_t result;
        std::visit([right, &result](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                result = (l + std::get<double>(right));
            } else {
                INFO("not support operation `+` for type {}", typeid(T).name());
            }
        }, left);
        return result;
    }},
    {'-', [](const context_t& left, const context_t& right) {
        context_t result;
        std::visit([right, &result](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                result = (l - std::get<double>(right));
            } else {
                INFO("not support operation `-` for type {}", typeid(T).name());
            }
        }, left);
        return result;
    }},
    {'*', [](const context_t& left, const context_t& right) {
        context_t result;
        std::visit([right, &result](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                result = (l * std::get<double>(right));
            } else {
                INFO("not support operation `*` for type {}", typeid(T).name());
            }
        }, left);
        return result;
    }},
    {'/', [](const context_t& left, const context_t& right) {
        context_t result;
        std::visit([right, &result](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                auto r = std::get<double>(right);
                if (std::abs(r) < 1e-10) {
                    WARN("Division by zero detected for symbol: {}", r);
                    result = 0.0;
                } else { [[likely]]
                    result = (l / r);
                }
            } else {
                INFO("not support operation `/` for type {}", typeid(T).name());
            }
        }, left);
        return result;
    }},
};

using EvalPtr = context_t (FormulaParser::*)(const symbol_t&, const peg::Ast& , DataContext&);

Map<String, EvalPtr> evalMap{
    {"Number", &FormulaParser::evalNumber},
    {"Identifier", &FormulaParser::evalIdentifier},
    {"CompareExpr", &FormulaParser::evalComparison},
    {"FunctionCall", &FormulaParser::evalFunctionCall},
    {"Term", &FormulaParser::evalTerm},
    {"Program", &FormulaParser::evalProgram},
    {"Statement", &FormulaParser::evalStatement},
    {"AndExpr", &FormulaParser::evalAndExpr},
    {"OrExpr", &FormulaParser::evalOrExpr},
    {"NotExpr", &FormulaParser::evalNotExpr},
    {"Primary", &FormulaParser::evalPrimary},
    {"ArithExpr", &FormulaParser::evalArithmetic},
    {"ExpressionStmt", &FormulaParser::evalStatement}
};

bool check_bool(const context_t& feature) {
    bool result = false;
    std::visit([&result](auto&& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, double>) {
            if (val != 0) result = true;
        }
        else if constexpr (std::is_same_v<T, bool>) {
            result = val;
        }
    }, feature);
    return result;
}

} // anonymous namespace

// ========== CrossSectionGraph 实现 ==========

CrossSectionNode& CrossSectionGraph::addNode(const String& id) {
    auto& node = nodes[id];
    node.id = id;
    return node;
}

void CrossSectionGraph::addEdge(const String& from, const String& to) {
    nodes[from].dependencies.push_back(to);
    nodes[to].inDegree++;
}

bool CrossSectionGraph::topologicalSort() {
    evalOrder.clear();
    std::queue<String> queue;

    // 将所有入度为 0 的节点加入队列
    for (auto& [id, node] : nodes) {
        if (node.inDegree == 0) {
            queue.push(id);
        }
    }

    while (!queue.empty()) {
        String current = queue.front();
        queue.pop();
        evalOrder.push_back(current);

        // 减少依赖节点的入度
        for (auto& depId : nodes[current].dependencies) {
            auto it = nodes.find(depId);
            if (it != nodes.end()) {
                it->second.inDegree--;
                if (it->second.inDegree == 0) {
                    queue.push(depId);
                }
            }
        }
    }

    // 如果所有节点都被访问，说明无环
    return evalOrder.size() == nodes.size();
}

void CrossSectionGraph::clear() {
    nodes.clear();
    evalOrder.clear();
}

// ========== FormulaParser 方法实现 ==========

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
    if (!_parser.load_grammar(grammar)) {
        return ;
    }
    _parser.enable_ast();
}

bool FormulaParser::parse(const String& code) {
    _codes = cleanInputString(code);
    if (_parser.parse(_codes, _ast)) {
        _ast = _parser.optimize_ast(_ast);

#ifdef _DEBUG
        INFO("AST nodes count: {}", _ast->nodes.size());
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
        TradeAction action = (check_bool(exprValue) ? _default : TradeAction::HOLD);
        decisions.emplace_back(symbol, action);
    }

    return decisions;
}

context_t FormulaParser::eval(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    return evalNode(symbol, ast, context);
}

context_t FormulaParser::evalNumber(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    return ast.token_to_number<double>();
}

context_t FormulaParser::evalIdentifier(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    String token(ast.token);

    // 检查是否是截面函数调用
    if (_varToNodeId.count(token)) {
        String nodeId = _varToNodeId[token];
        auto it = _csGraph.nodes.find(nodeId);
        if (it != _csGraph.nodes.end() && it->second.computed && it->second.outputs.count(symbol)) {
            return it->second.outputs.at(symbol);
        }
    }

    // 原有逻辑
    auto name = get_symbol(symbol);
    auto key = name + "." + to_utf8(String(ast.token));
    if (context.exist(key)) {
        auto val = context.get(key);
        return val;
    }
    return String(ast.token);
}

context_t FormulaParser::evalComparison(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto left = eval(symbol, *ast.nodes[0], context);
    auto right = eval(symbol, *ast.nodes[2], context);
    String op(ast.nodes[1]->token);
    auto result = comparationMap[op](left, right);
    return result;
}

context_t FormulaParser::evalTerm(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (ast.nodes.size() == 1) {
        return evalNode(symbol, *ast.nodes.front(), context);
    }
    else if (ast.nodes.size() >= 3) {
        return evalArithmetic(symbol, ast, context);
    }
    return 0.;
}

context_t FormulaParser::evalProgram(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (ast.nodes.empty())
        return 0.;

    context_t last_result;
    for (auto& stmt : ast.nodes) {
        if (stmt->name == "EOL")
            continue;

        last_result = evalStatement(symbol, *stmt, context);
    }
    return last_result;
}

context_t FormulaParser::evalStatement(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (ast.name == "ExpressionStmt") {
        return evalNode(symbol, *ast.nodes[0], context);
    }
    else if (ast.name == "AssignmentStmt") {
        String vaName(ast.nodes[0]->token);
        context_t value = evalNode(symbol, *ast.nodes[1], context);
    }
    else if (ast.name == "EOF") {
        return 0.;
    } else {
        WARN("not support statement {}", ast.name);
        return 0.;
    }
}

context_t FormulaParser::evalPrimary(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto value = evalNode(symbol, *ast.nodes.front(), context);
    for (size_t i = 1; i < ast.nodes.size(); ++i) {
        auto& trailer = ast.nodes[i];
        if (trailer->name == "Trailer") {
            value = evalTrailer(symbol, value, *trailer, context);
        }
        else if (trailer->name == "TimeOffset") {
            value = evalTimeIndex(symbol, value, *trailer, context);
        }
    }
    return value;
}

context_t FormulaParser::evalTrailer(const symbol_t& symbol, const context_t& base, const peg::Ast& ast, DataContext& context) {
    if (ast.nodes.empty()) return base;

    auto& trailer_type = ast.nodes[0];
    if (trailer_type->name == "TimeOffset") {
        return evalTimeIndex(symbol, base, *trailer_type, context);
    }
    return base;
}

context_t FormulaParser::evalTimeIndex(const symbol_t& symbol, const context_t& base, const peg::Ast& ast, DataContext& context) {
    int time_offset = 0;
    String token(ast.token);

    if (token == "t") {
        time_offset = 0;
    } else if (token.size() > 1 && token[0] == 't' && token[1] == '-') {
        try {
            double num = std::stod(token.substr(2));
            time_offset = -static_cast<int>(num);
        } catch (...) {
            WARN("Invalid time offset: {}", token);
            time_offset = 0;
        }
    } else {
        try {
            double num = std::stod(token);
            time_offset = static_cast<int>(num);
        } catch (...) {
            WARN("Invalid time index: {}", token);
            time_offset = 0;
        }
    }

    return getHistoricalValue(symbol, base, time_offset, context);
}

double FormulaParser::getHistoricalValue(const symbol_t& symbol, const context_t& base, int time_offset, DataContext& context) {
    if (std::holds_alternative<Vector<double>>(base)) {
        auto& vec = std::get<Vector<double>>(base);
        if (vec.empty()) {
            WARN("getHistoricalValue - empty vector");
            return 0.0;
        }
        int idx = (int)vec.size() - 1 + time_offset;
        if (idx >= 0 && idx < (int)vec.size()) {
            return vec[idx];
        } else {
            WARN("getHistoricalValue - index out of range, idx={}, size={}", idx, vec.size());
            return vec.back();
        }
    }

    String var_name = std::get<String>(base);
    auto name = get_symbol(symbol);
    String key = name + "." + var_name;

    auto& vec = context.get<Vector<double>>(key);
    int idx = (int)vec.size() - 1 + time_offset;
    if (idx >= 0 && idx < (int)vec.size()) {
        return vec[idx];
    } else {
        return vec.back();
    }
}

context_t FormulaParser::evalOrExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto left = evalNode(symbol, *ast.nodes[0], context);
    if (check_bool(left)) return true;

    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        auto right = evalNode(symbol, *ast.nodes[i], context);
        if (check_bool(right)) return true;
    }
    return false;
}

context_t FormulaParser::evalAndExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto left = evalNode(symbol, *ast.nodes[0], context);
    if (check_bool(left) == false) return false;

    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        auto right = evalNode(symbol, *ast.nodes[i], context);
        if (check_bool(right) == false) return false;
    }
    return true;
}

context_t FormulaParser::evalNotExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (ast.nodes.size() == 2) {
        auto value = evalNode(symbol, *ast.nodes[1], context);
        return !check_bool(value);
    } else {
        return evalNode(symbol, *ast.nodes[0], context);
    }
}

context_t FormulaParser::evalNode(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (evalMap.count(ast.name) == 0) {
        INFO("ast node `{}` not found", ast.name);
        return false;
    }

    return (this->*(evalMap[ast.name]))(symbol, ast, context);
}

context_t FormulaParser::evalFunctionCall(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto funcName = String(ast.nodes[0]->token);

    // 如果是截面函数，在 envokeMixedCase 中已经预计算，直接从 context 读取
    if (isCrossSectionFunction(funcName)) {
        if (_varToNodeId.count(funcName)) {
            String nodeId = _varToNodeId[funcName];
            auto it = _csGraph.nodes.find(nodeId);
            if (it != _csGraph.nodes.end() && it->second.computed && it->second.outputs.count(symbol)) {
                return it->second.outputs.at(symbol);
            }
        }
        return false;
    }

    // 其他函数调用处理（如 MA 等）
    if (funcName == "MA" && ast.nodes.size() >= 3) {
        // 获取参数：MA(close, 5)
    }
    else if (funcName == INTRINSIC_TOPK) {
        if (ast.nodes.size() < 2) {
            WARN("topk function requires two arguments");
            return false;
        }
        auto& args = ast.nodes[1];
        if (args->name != "Arguments" || args->nodes.size() != 2) {
            WARN("topk function requires exactly two arguments");
            return false;
        }
        auto& firstArg = args->nodes[0];
        context_t scoreExprValue = evalNode(symbol, *firstArg, context);
        auto& secondArg = args->nodes[1];
        context_t secondValue = evalNode(symbol, *secondArg, context);
        String varName;
        if (std::holds_alternative<String>(scoreExprValue)) {
            varName = std::get<String>(scoreExprValue);
        } else {
            WARN("First argument of topk should be a variable name");
            return false;
        }
        int k = 0;
        if (std::holds_alternative<double>(secondValue)) {
            k = static_cast<int>(std::get<double>(secondValue));
        } else {
            WARN("Second argument of topk should be a number");
            return false;
        }
    }
    return 0.;
}

context_t FormulaParser::getVariableValue(const symbol_t& symbol, const String& varName, DataContext* context) {
    auto str = get_symbol(symbol);
    String key = str + "." + varName;
    return context->get(key);
}

context_t FormulaParser::evalArithmetic(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto result = evalNode(symbol, *ast.nodes[0], context);
    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        char op = ast.nodes[i]->token[0];
        auto operand = evalNode(symbol, *ast.nodes[i + 1], context);
        result = arithmeticMap[op](result, operand);
    }

    return result;
}
