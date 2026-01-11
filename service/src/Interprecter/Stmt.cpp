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
        CompareExpr     <- BitwiseOrExpr (CompareOp BitwiseOrExpr)*
        BitwiseOrExpr   <- ArithExpr
        ArithExpr       <- Term (AddOp Term)*
        Term            <- Factor (MulOp Factor)*
        Factor          <- Primary (FactorOp Primary)*
        Primary         <- Atom (Trailer)*
        Atom            <- Number / String / FunctionCall / ListExpr / Identifier / '(' Expression ')'

        # 时间序列访问
        Trailer         <- '.' Identifier / '(' Arguments? ')' / '[' TimeIndex ']'
        TimeIndex       <- TimeOffset
        TimeOffset      <- HistoricalTime / CurrentTime / PureNumber
        CurrentTime     <- 't'
        HistoricalTime  <- 't' '-' PureNumber
        PureNumber      <- < [0-9]+ >

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
        CompareOp       <- '==' / '!=' / '<' / '<=' / '>' / '>=' 
        AddOp           <- '+' / '-'
        MulOp           <- '*' / '@' / '/' / '//' / '%'
        FactorOp        <- '**'
        
        # 语句分隔符
        EOL             <- ';' [ \t\r\n]* / !.
        %whitespace     <- [ \t]*
    )";

Map<String, std::function<bool (const feature_t& , const feature_t& )>> comparationMap{
    {">", [](const feature_t& left, const feature_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = l > std::get<double>(right);
            }
        }, left);
        return val;
    }},
    {"<", [](const feature_t& left, const feature_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = l < std::get<double>(right);
            }
        }, left);
        return val;
    }},
    {"==", [](const feature_t& left, const feature_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = (l == std::get<double>(right));
            }
        }, left);
        return val;
    }},
    {"!=", [](const feature_t& left, const feature_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = (l != std::get<double>(right));
            }
        }, left);
        return val;
    }},
    {">=", [](const feature_t& left, const feature_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = (l >= std::get<double>(right));
            }
        }, left);
        return val;
    }},
    {"<=", [](const feature_t& left, const feature_t& right) {
        bool val = false;
        std::visit([right, &val](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                val = (l <= std::get<double>(right));
            }
        }, left);
        return val;
    }},
};

Map<char, std::function<feature_t(const feature_t& , const feature_t&)>> arithmeticMap{
    {'+', [](const feature_t& left, const feature_t& right) {
        feature_t result;
        std::visit([right, &result](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                result = (l + std::get<double>(right));
            }
        }, left);
        return result;
    }},
    {'-', [](const feature_t& left, const feature_t& right) {
        feature_t result;
        std::visit([right, &result](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                result = (l - std::get<double>(right));
            }
        }, left);
        return result;
    }},
    {'*', [](const feature_t& left, const feature_t& right) {
        feature_t result;
        std::visit([right, &result](auto&& l){
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, double>) {
                result = (l * std::get<double>(right));
            }
        }, left);
        return result;
    }},
    {'/', [](const feature_t& left, const feature_t& right) {
        feature_t result;
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
            }
        }, left);
        return result;
    }},
};

using EvalPtr = feature_t (FormulaParser::*)(const symbol_t&, const peg::Ast& , DataContext&);

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

bool check_bool(const feature_t& feature) {
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
    feature_t kValue = evalNode(symbol_t{}, *kExprAst, context);
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
        INFO("{}Node: {}", tabs, node->name);
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

feature_t FormulaParser::evaluateForSymbolWithCrossSectionResults(const symbol_t& symbol, const peg::Ast& ast, DataContext& context,
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
    if (hasCrossSectionFunctions(*_ast)) {
        return envokeMixedCase(symbols, variantNames, context);
    } else {
        for (auto symbol: symbols) {
            // try {
                auto exprValue = eval(symbol, *_ast, context);
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

feature_t FormulaParser::eval(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    return evalNode(symbol, ast, context);
}

feature_t FormulaParser::evalNumber(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    return ast.token_to_number<double>();
}

feature_t FormulaParser::evalIdentifier(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    // auto name = get_symbol(symbol);
    // auto key = name + "." + to_utf8(String(ast.token));
    // return context.get(key);
    return String(ast.token);
}

feature_t FormulaParser::evalComparison(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    // 节点：COMPARISON -> TERM COMP_OPERATOR TERM
    // 子节点：三个，左操作数、运算符、右操作数
    auto left = eval(symbol, *ast.nodes[0], context);
    auto right = eval(symbol, *ast.nodes[2], context);
    String op(ast.nodes[1]->token);
    return comparationMap[op](left, right);
}

feature_t FormulaParser::evalTerm(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    // 节点：TERM -> FACTOR (TERM_OPERATOR FACTOR)*
    // 子节点：至少一个，然后是多个（运算符，因子）
    if (ast.nodes.size() == 1) {
        return evalArithmetic(symbol, *ast.nodes.front(), context);
    }
    else if (ast.nodes.size() == 3) {
        return evalArithmetic(symbol, ast, context);
    }
    return 0.;
}

feature_t FormulaParser::evalProgram(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (ast.nodes.empty())
        return 0.;

    feature_t last_result;
    for (auto& stmt : ast.nodes) {
        if (stmt->name == "EOL")
            continue;

        last_result = evalStatement(symbol, *stmt, context);
    }
    return last_result;
}

feature_t FormulaParser::evalStatement(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (ast.name == "ExpressionStmt") {
        return evalNode(symbol, *ast.nodes[0], context);
    }
    else if (ast.name == "AssignmentStmt") {
        // 处理赋值语句：identifier = expression
        String vaName(ast.nodes[0]->token);
        feature_t value = evalNode(symbol, *ast.nodes[1], context);
    }
    else if (ast.name == "EOF") {
        return 0.;
    } else {
        WARN("not support statement {}", ast.name);
        return 0.;
    }
}

feature_t FormulaParser::evalPrimary(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto value = evalNode(symbol, *ast.nodes.front(), context);
    // 处理 Trailer（包括时间索引）,此时value应该是数组
    for (size_t i = 1; i < ast.nodes.size(); ++i) {
        auto& trailer = ast.nodes[i];
        if (trailer->name == "Trailer") {
            value = evalTrailer(symbol, value, *trailer, context);
        }
        else if (trailer->name == "PureNumber") {
            int offset = atoi(String(trailer->token).c_str());
            value = getHistoricalValue(symbol, value, offset, context);
            // value = evalTimeIndex(symbol, value, *trailer->nodes[0], context);
        }
        else if (trailer->name == "CurrentTime") {
            value = getHistoricalValue(symbol, value, 0, context);
        }
    }
    return value;
}

feature_t FormulaParser::evalTrailer(const symbol_t& symbol, const feature_t& base, const peg::Ast& ast, DataContext& context) {
    if (ast.nodes.empty()) return base;
    
    auto& trailer_type = ast.nodes[0];
    if (trailer_type->name == "TimeIndex") {
        // 处理时间索引 [t], [t-1], [1] 等
        return evalTimeIndex(symbol, base, *trailer_type, context);
    }
    // 其他类型的 trailer...
    return base;
}

feature_t FormulaParser::evalTimeIndex(const symbol_t& symbol, const feature_t& base, const peg::Ast& ast, DataContext& context) {
    int time_offset = 0;
    
    // 检查是否有子节点
    if (ast.nodes.empty()) {
        // 没有子节点，直接检查 token
        if (ast.token == "t") {
            time_offset = 0;
        } else {
            // 尝试解析为数字
            try {
                double num = std::stod(String(ast.token));
                time_offset = -static_cast<int>(num);
            } catch (...) {
                WARN("Invalid time index: {}", ast.token);
                time_offset = 0;
            }
        }
    } else {
        // 有子节点的情况
        if (ast.nodes[0]->name == "TimeOffset") {
            auto& time_offset_ast = *ast.nodes[0];
            
            if (time_offset_ast.token == "t") {
                time_offset = 0;
                // 检查是否有加减操作
                if (time_offset_ast.nodes.size() == 2) {
                    std::string op(time_offset_ast.nodes[0]->token);
                    double num = std::get<double>(evalNumber(symbol, *time_offset_ast.nodes[1], context));
                    if (op == "-") {
                        time_offset = -static_cast<int>(num);
                    }
                }
            } else {
                // 纯数字
                try {
                    double num = std::stod(String(time_offset_ast.token));
                    time_offset = -static_cast<int>(num);
                } catch (...) {
                    WARN("Invalid time offset: {}", time_offset_ast.token);
                    time_offset = 0;
                }
            }
        }
    }
    // 这里需要根据 base（标识符名称）和时间偏移获取对应的历史值
    // 需要扩展 DataContext 支持历史数据访问
    return getHistoricalValue(symbol, base, time_offset, context);
}

double FormulaParser::getHistoricalValue(const symbol_t& symbol, const feature_t& base, int time_offset, DataContext& context) {
    // 假设 base 包含变量名信息，或者需要从其他地方获取变量名
    // 这里需要根据变量名和时间偏移获取历史值
    /* 从 base 中提取变量名 */
    String var_name = std::get<String>(base);
    auto name = get_symbol(symbol);
    String key = name + "." + var_name;
    
    // 扩展 DataContext 支持历史数据访问
    auto& vec = context.get<Vector<double>>(key);
    // time_offset 0,-1, ...
    int idx = vec.size() - 1 - time_offset;
    if (idx >= 0) {
        return vec[idx];
    } else {
        return vec.back();
    }
}

feature_t FormulaParser::evalOrExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto left = evalNode(symbol, *ast.nodes[0], context);
    if (check_bool(left)) return true;  // 短路求值

    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        auto right = evalNode(symbol, *ast.nodes[i], context);
        if (check_bool(right)) return true;
    }
    return false;
}

feature_t FormulaParser::evalAndExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto left = evalNode(symbol, *ast.nodes[0], context);
    if (check_bool(left) == false) return false;  // 短路求值
    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        auto right = evalNode(symbol, *ast.nodes[i], context);
        if (check_bool(right) == false) return false;
    }
    return true;
}

feature_t FormulaParser::evalNotExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (ast.nodes.size() == 2) {
        // not expr
        auto value = evalNode(symbol, *ast.nodes[1], context);
        return !check_bool(value);
    } else {
        // 没有not
        return evalNode(symbol, *ast.nodes[0], context);
    }
}

feature_t FormulaParser::evalNode(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (evalMap.count(ast.name) == 0) {
        INFO("ast node `{}` not found", ast.name);
        return false;
    }

    return (this->*(evalMap[ast.name]))(symbol, ast, context);
    
    // else if (ast.name == "Expression") {
        
    //     return eval(symbol, *ast.nodes.front(), context);
    // }
    // else if (ast.name == "FactorOp") {
        
    //     return eval(symbol, *ast.nodes.front(), context);
    // }
    // else if (ast.name == "Term") {
        
    // }
    // else if (ast.name == "Factor") {
    //     // 节点：FACTOR -> PRIMARY (FACTOR_OPERATOR PRIMARY)*
    //     return eval(symbol, *ast.nodes.front(), context);
    // }
    // else if (ast.name == "Primary") {
    //     // 只有一个子节点，直接求值
    //     return eval(symbol, *ast.nodes.front(), context);
    // }
    return 0.0;
}

feature_t FormulaParser::evalFunctionCall(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
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
        feature_t scoreExprValue = evalNode(symbol, *firstArg, context);
        // 解析第二个参数：k值
        auto& secondArg = args->nodes[1];
        feature_t secondValue = evalNode(symbol, *secondArg, context);
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

feature_t FormulaParser::evalArithmetic(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
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
