#include "Interprecter/Stmt.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "peglib.h"
#include "server.h"
#include <cstdint>
#include <functional>
#include <variant>
#include <stack>

#define ANY_CAST(val) any_cast<std::shared_ptr<Stmt>>(val)

#define INTRINSIC_TOPK  "topk"
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

        # 技术指标专用函数
        # TechFunction    <- 'cross_above' / 'cross_below'

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

}

void FormulaParser::topk(const Vector<symbol_t>& allSymbols, const peg::Ast& funcAst, CrossSectionResult& result, DataContext& context) {
    // 解析topk参数
    auto& args = funcAst.nodes[1]; // Arguments节点
    
    if (args->nodes.size() != 2) {
        WARN("topk requires 2 arguments");
        return;
    }
    
    auto& scoreExprAst = args->nodes[0];
    auto& kExprAst = args->nodes[1];
    
    // 计算k值
    context_t kValue = evalNode(symbol_t{}, *kExprAst, context);
    int k = 10; // 默认值
    if (std::holds_alternative<double>(kValue)) {
        k = static_cast<int>(std::get<double>(kValue));
    }
    
    // 计算每个symbol的分数
    Vector<std::pair<symbol_t, double>> scores;
    for (auto symbol : allSymbols) {
        // double score = computeScoreForSymbol(symbol, *scoreExprAst, context);
        // scores.emplace_back(symbol, score);
    }
    
    // 排序并取前k个
    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
    
    // 存储结果
    Set<symbol_t> topKSymbols;
    for (int i = 0; i < std::min(k, static_cast<int>(scores.size())); ++i) {
        topKSymbols.insert(scores[i].first);
    }
    
    for (auto symbol : allSymbols) {
        result.stockResults[symbol] = (topKSymbols.count(symbol) > 0);
    }
}

String FormulaParser::cleanInputString(const String& input) {
    String result;
    for (char c : input) {
        // 只保留 ASCII 可打印字符和必要的运算符
        if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r') {
            result += c;
        } else {
            // 替换非 ASCII 字符或记录警告
            // WARN("Non-ASCII character detected and removed: {}", static_cast<int>(static_cast<unsigned char>(c)));
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
    // _parser.enable_trace();
    if (!_parser.load_grammar(grammar)) {
        return ;
    }
    _parser.enable_ast();
}

bool FormulaParser::parse(const String& code) {
    _codes = cleanInputString(code);
    if (_parser.parse(_codes, _ast)) {
        _ast = _parser.optimize_ast(_ast);

        // 调试：打印AST结构
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

bool FormulaParser::isCrossSectionFunction(const String& funName) {
    static const UnorderedSet<String> crossFuncs{
        INTRINSIC_TOPK,
    };
    return crossFuncs.count(funName);
}

bool FormulaParser::hasCrossSectionFunctions(const peg::Ast& ast) {
    thread_local bool hasCrossFunc = false;
    if (hasCrossFunc)
        return true;

    std::stack<const peg::Ast*> stack;
    stack.push(&ast);
    
    while (!stack.empty()) {
        const peg::Ast* current = stack.top();
        stack.pop();
        
        // 检查当前节点
        if (current->name == "FunctionCall") {
            if (!current->nodes.empty()) {
                auto& firstChild = current->nodes[0];
                if (firstChild->name == "Identifier") {
                    String funcName(firstChild->token);
                    if (isCrossSectionFunction(funcName)) {
                        hasCrossFunc = true;
                        return true;
                    }
                }
            }
        }
        
        // 添加子节点到栈中
        for (auto it = current->nodes.rbegin(); it != current->nodes.rend(); ++it) {
            stack.push(it->get());
        }
    }
    return false;
}

void FormulaParser::precomputeCrossSectionFunctions(const Vector<symbol_t>& symbols, DataContext& context) {
    // Map<String, std::shared_ptr<CrossSectionResult>> results;
    for (const auto& [varName, func] : _CSFunctions) {
        if (func._name == INTRINSIC_TOPK) {
            auto arg1 = std::get<String>(func._args.at(0));
            auto arg2 = func._args.at(1);
            Map<double, symbol_t> scores;
            for (auto symbol: symbols) {
                String key = arg1 + "." + get_symbol(symbol);
                auto& vec = context.get<Vector<double>>(key);
                scores[vec.back()] = symbol;
            }
            // topk(symbols, *funcAst, *result, context);
        }
        // results[funcName] = result;
    }
    // return results;
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

            // 生成对应的函数对象
            auto& func = _CSFunctions[funcName];
            func._name = funcName;
            for (int i = 1; i < node.nodes.size(); ++i) {
                auto arguments = node.nodes[i];
                for (int loc = 0; loc < arguments->nodes.size(); ++loc) {
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

List<Pair<symbol_t, TradeAction>> FormulaParser::envoke(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context) {
    List<Pair<symbol_t, TradeAction>> decisions;
    // INFO("FormulaParser::envoke - symbols count={}, variantNames count={}", symbols.size(), variantNames.size());
    if (hasCrossSectionFunctions(*_ast)) {
        return envokeMixedCase(symbols, variantNames, context);
    } else {
        for (auto symbol: symbols) {
            // try {
                auto exprValue = eval(symbol, *_ast, context);
                // INFO("FormulaParser::envoke - symbol={}, exprValue type={}, value={}", get_symbol(symbol), exprValue.index(), std::get<bool>(exprValue) ? "true" : "false");
                Pair<symbol_t, TradeAction> action{
                    symbol, (std::get<bool>(exprValue)? _default: TradeAction::HOLD)
                };
                decisions.emplace_back(std::move(action));
            // } catch (const std::exception& e) {
            //     FATAL("envoke error: {}", e.what());
            //     continue;
            // }
        }
    }
    return decisions;
}

List<Pair<symbol_t, TradeAction>> FormulaParser::envokeMixedCase(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context) {
    List<Pair<symbol_t, TradeAction>> decisions;
    // 预计算所有截面函数
    extractCrossSectionFunctions(*_ast);
    precomputeCrossSectionFunctions(symbols, context);
    // 为每个symbol求值
    for (auto symbol : symbols) {
        // auto exprValue = evaluateForSymbolWithCrossSectionResults(
        //     symbol, *_ast, context, crossSectionResults);
        
        // Pair<symbol_t, TradeAction> action{
        //     symbol, (std::get<bool>(exprValue) ? _default : TradeAction::HOLD)
        // };
        // decisions.emplace_back(std::move(action));
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
    auto name = get_symbol(symbol);
    auto key = name + "." + to_utf8(String(ast.token));
    // INFO("FormulaParser::evalIdentifier - symbol={}, token={}, key={}", name, ast.token, key);
    // INFO("FormulaParser::evalIdentifier - context.exist(key)={}", context.exist(key));
    if (context.exist(key)) {
        auto val = context.get(key);
        return val;
    }
    // 回退：返回变量名（用于后续 Trailer 处理）
    // INFO("FormulaParser::evalIdentifier - key not found, returning token string: {}", ast.token);
    return String(ast.token);
}

context_t FormulaParser::evalComparison(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    // 节点：COMPARISON -> TERM COMP_OPERATOR TERM
    // 子节点：三个，左操作数、运算符、右操作数
    auto left = eval(symbol, *ast.nodes[0], context);
    auto right = eval(symbol, *ast.nodes[2], context);
    String op(ast.nodes[1]->token);
    // INFO("FormulaParser::evalComparison - op={}, left type={}, right type={}", op, left.index(), right.index());
    auto result = comparationMap[op](left, right);
    // INFO("FormulaParser::evalComparison - result={}", (result) ? "true" : "false");
    return result;
}

context_t FormulaParser::evalTerm(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    // 节点：TERM -> PRIMARY (MUL_OP PRIMARY)*
    // 子节点：至少一个，然后是多个（运算符，操作数）
    if (ast.nodes.size() == 1) {
        // 只有一个 Primary 子节点，直接求值
        return evalNode(symbol, *ast.nodes.front(), context);
    }
    else if (ast.nodes.size() >= 3) {
        // 有乘法/除法运算符，使用通用算术处理
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
        // 处理赋值语句：identifier = expression
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
    // INFO("evalPrimary - initial value type={}, has {} trailers", value.index(), ast.nodes.size() - 1);
    for (size_t i = 1; i < ast.nodes.size(); ++i) {
        auto& trailer = ast.nodes[i];
        if (trailer->name == "Trailer") {
            // INFO("evalPrimary - processing Trailer");
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
        // 处理时间索引 [t], [t-1], [0] 等
        return evalTimeIndex(symbol, base, *trailer_type, context);
    }
    // 其他类型的 trailer（如 '.identifier' 或 '(...)'）...
    return base;
}

context_t FormulaParser::evalTimeIndex(const symbol_t& symbol, const context_t& base, const peg::Ast& ast, DataContext& context) {
    int time_offset = 0;

    // 文法：TimeOffset <- 't' '-' [0-9]+ / 't' / [0-9]+
    // token 可能是 "t", "t-1", "t-2", "0", "1" 等
    String token(ast.token);

    if (token == "t") {
        // [t] - 当前时刻
        time_offset = 0;
    } else if (token.size() > 1 && token[0] == 't' && token[1] == '-') {
        // [t-1], [t-2] 等 - 历史时刻
        try {
            double num = std::stod(token.substr(2));
            time_offset = -static_cast<int>(num);
        } catch (...) {
            WARN("Invalid time offset: {}", token);
            time_offset = 0;
        }
    } else {
        // 纯数字 [0], [1] 等 - 向前偏移（正数表示向前）
        try {
            double num = std::stod(token);
            time_offset = static_cast<int>(num);
        } catch (...) {
            WARN("Invalid time index: {}", token);
            time_offset = 0;
        }
    }

    // 根据 base（标识符名称）和时间偏移获取对应的历史值
    return getHistoricalValue(symbol, base, time_offset, context);
}

double FormulaParser::getHistoricalValue(const symbol_t& symbol, const context_t& base, int time_offset, DataContext& context) {
    // base 可能是 Vector<double> 类型（从 evalIdentifier 返回的时间序列数据）
    // 或者 String 类型（变量名）
    if (std::holds_alternative<Vector<double>>(base)) {
        auto& vec = std::get<Vector<double>>(base);
        if (vec.empty()) {
            WARN("getHistoricalValue - empty vector");
            return 0.0;
        }
        // time_offset: 0=当前，-1=前一个，-2=前两个...
        // idx 计算：vec.size()-1 是当前值
        int idx = (int)vec.size() - 1 + time_offset;
        // INFO("getHistoricalValue - vec size={}, time_offset={}, idx={}", vec.size(), time_offset, idx);
        if (idx >= 0 && idx < (int)vec.size()) {
            return vec[idx];
        } else {
            WARN("getHistoricalValue - index out of range, idx={}, size={}", idx, vec.size());
            return vec.back();
        }
    }

    // 兼容旧的逻辑：base 是变量名
    String var_name = std::get<String>(base);
    auto name = get_symbol(symbol);
    String key = name + "." + var_name;

    // INFO("getHistoricalValue - using key: {}, time_offset={}", key, time_offset);
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
    if (check_bool(left)) return true;  // 短路求值

    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        auto right = evalNode(symbol, *ast.nodes[i], context);
        if (check_bool(right)) return true;
    }
    return false;
}

context_t FormulaParser::evalAndExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto left = evalNode(symbol, *ast.nodes[0], context);
    if (check_bool(left) == false) return false;  // 短路求值
    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        auto right = evalNode(symbol, *ast.nodes[i], context);
        if (check_bool(right) == false) return false;
    }
    return true;
}

context_t FormulaParser::evalNotExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (ast.nodes.size() == 2) {
        // not expr
        auto value = evalNode(symbol, *ast.nodes[1], context);
        return !check_bool(value);
    } else {
        // 没有not
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
    auto funcName = ast.nodes[0]->token;
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
        // 第一个参数应该是Identifier，获取变量名
        auto& firstArg = args->nodes[0];
        context_t scoreExprValue = evalNode(symbol, *firstArg, context);
        // 解析第二个参数：k值
        auto& secondArg = args->nodes[1];
        context_t secondValue = evalNode(symbol, *secondArg, context);
        // 获取变量名（从第一个参数）
        String varName;
        if (std::holds_alternative<String>(scoreExprValue)) {
            varName = std::get<String>(scoreExprValue);
        } else {
            // 如果不是字符串，尝试转换或使用默认方式
            WARN("First argument of topk should be a variable name");
            return false;
        }
        // 获取k值
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
    // 算术表达式结构：第一个操作数 [运算符 操作数]*
    auto result = evalNode(symbol, *ast.nodes[0], context);
    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        // 获取运算符
        char op = ast.nodes[i]->token[0];
        
        // 获取操作数
        auto operand = evalNode(symbol, *ast.nodes[i + 1], context);
        
        // 执行运算
        result = arithmeticMap[op](result, operand);
    }
    
    return result;
}
