#include "Interprecter/Stmt.h"
#include "Util/string_algorithm.h"
#include "server.h"
#include <cstdint>
#include <functional>
#include <variant>

#define ANY_CAST(val) any_cast<std::shared_ptr<Stmt>>(val)

namespace  {
String grammar = R"(
        # 表达式定义
        Expression      <- Comparison
        Comparison      <- Term (ComparisonOp Term)*
        Term            <- Factor (TermOp Factor)*
        Factor          <- Primary (FactorOp Primary)*
        Primary         <- Number / Identifier / FunctionCall / '(' Expression ')'
        
        # 函数调用
        FunctionCall    <- Identifier '(' Arguments? ')'
        Arguments       <- Expression (',' Expression)*
        
        # 标识符和数字
        Identifier      <- < [a-zA-Z_][a-zA-Z_0-9]* >
        Number          <- < '-'? [0-9]+ ('.' [0-9]+)? >
        
        # 运算符
        ComparisonOp    <- "<=" / ">=" / "==" / "!=" / "<" / ">"
        TermOp          <- < [-+] >
        FactorOp        <- < [*/] >
        
        %whitespace     <- [ \t\r\n]*
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

}

double FunctionCallStmt::evaluate(Server* server) const {
    return 0;
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
        INFO("{} {}: {}", line, col, msg);
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
    std::shared_ptr<peg::Ast> ast;
    if (_parser.parse(_codes, ast)) {
        _ast = _parser.optimize_ast(ast);
        return true;
    } else {
        FATAL("Parse failed for formula: {}", _codes);
        return false;
    }
}

bool FormulaParser::parse(const String& code, TradeAction action) {
    _default = action;
    return parse(code);
}

List<TradeDecision> FormulaParser::envoke(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context) {
    List<TradeDecision> decisions;
    for (auto symbol: symbols) {
        try {
            auto exprValue = eval(symbol, *_ast, context);
            auto decision = makeDecision(symbol, std::get<double>(exprValue), context);
            decisions.emplace_back(std::move(decision));
        } catch (const std::exception& e) {
            FATAL("envoke error: {}", e.what());
            continue;
        }
    }
    return decisions;
}

feature_t FormulaParser::eval(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    return evalNode(symbol, ast, context);
}

feature_t FormulaParser::evalNode(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (ast.name == "Number") {
        return ast.token_to_number<double>();
    }
    else if (ast.name == "Identifier") {
        auto name = get_symbol(symbol);
        auto key = name + "." + to_utf8(String(ast.token));
        return context.get(key);
    }
    else if (ast.name == "Comparison") {
        // 节点：COMPARISON -> TERM COMP_OPERATOR TERM
        // 子节点：三个，左操作数、运算符、右操作数
        auto left = eval(symbol, *ast.nodes[0], context);
        auto right = eval(symbol, *ast.nodes[2], context);
        String op(ast.nodes[1]->token);
        return comparationMap[op](left, right);
    }
    else if (ast.name == "FunctionCall") {
        // 节点：FUNCTION_CALL -> 'MA' '(' IDENTIFIER ',' EXPRESSION ')'
        // 子节点：第一个是IDENTIFIER（函数名），第二个是IDENTIFIER（变量名），第三个是EXPRESSION（周期）
        auto func_name = ast.nodes[0]->token;
        if (func_name == "MA") {

        }
    }
    else if (ast.name == "Expression") {
        
        return eval(symbol, *ast.nodes.front(), context);
    }
    else if (ast.name == "FactorOp") {
        
        return eval(symbol, *ast.nodes.front(), context);
    }
    else if (ast.name == "Term") {
        // 节点：TERM -> FACTOR (TERM_OPERATOR FACTOR)*
        // 子节点：至少一个，然后是多个（运算符，因子）
        if (ast.nodes.size() == 1) {
            return evalArithmetic(symbol, *ast.nodes.front(), context, "Term");
        }
        else if (ast.nodes.size() == 3) {
            return evalArithmetic(symbol, ast, context, "Term");
        }
    }
    else if (ast.name == "Factor") {
        // 节点：FACTOR -> PRIMARY (FACTOR_OPERATOR PRIMARY)*
        return eval(symbol, *ast.nodes.front(), context);
    }
    else if (ast.name == "Primary") {
        // 只有一个子节点，直接求值
        return eval(symbol, *ast.nodes.front(), context);
    }
    return 0.0;
}

double FormulaParser::evalFunctionCall(const symbol_t& symbol, const peg::Ast& ast, DataContext* context) {
    auto funcName = ast.nodes[0]->token;
    if (funcName == "MA" && ast.nodes.size() >= 3) {
        // 获取参数：MA(close, 5)
    }
    return 0;
}

feature_t FormulaParser::getVariableValue(const symbol_t& symbol, const String& varName, DataContext* context) {
    auto str = get_symbol(symbol);
    String key = str + "." + varName;
    return context->get(key);
}

TradeDecision FormulaParser::makeDecision(const symbol_t& symbol, double exprValue, DataContext& context) {
    TradeDecision d;
    return d;
}

feature_t FormulaParser::evalArithmetic(const symbol_t& symbol, const peg::Ast& ast, DataContext& context, const String& nodeType) {
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
