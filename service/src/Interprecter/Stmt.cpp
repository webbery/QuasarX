#include "Interprecter/Stmt.h"
#include "Util/string_algorithm.h"
#include "server.h"

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
        ComparisonOp    <- < [<>]=? | '==' | '!=' >
        TermOp          <- < [-+] >
        FactorOp        <- < [*/] >
        
        %whitespace     <- [ \t\r\n]*
    )";
}

double FunctionCallStmt::evaluate(Server* server) const {
    return 0;
}

FormulaParser::FormulaParser(Server* server): _server(server), _default(TradeAction::HOLD) {
    if (!_parser.load_grammar(grammar)) {
        return ;
    }
    _parser.enable_ast();
}

bool FormulaParser::parse(const String& code) {
    if (_parser.parse(code, _ast)) {
        _ast = _parser.optimize_ast(_ast);
        return true;
    } else {
        FATAL("Parse failed for formula: {}", code);
        return false;
    }
}

bool FormulaParser::parse(const String& code, TradeAction action) {
    _default = action;
    return parse(code);
}

List<TradeDecision> FormulaParser::envoke(const Vector<symbol_t>& symbols, const List<String>& variantNames, DataContext* context) {
    List<TradeDecision> decisions;
    for (auto symbol: symbols) {
        try {
            double exprValue = eval(symbol, *_ast, context);
            auto decision = makeDecision(symbol, exprValue, context);
            decisions.emplace_back(std::move(decision));
        } catch (const std::exception& e) {
            FATAL("envoke error: {}", e.what());
            continue;
        }
    }
    return decisions;
}

double FormulaParser::eval(const symbol_t& symbol, const peg::Ast& ast, DataContext* context) {
    return evalNode(symbol, ast, context);
}

double FormulaParser::evalNode(const symbol_t& symbol, const peg::Ast& ast, DataContext* context) {
    if (ast.name == "NUMBER") {
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

TradeDecision FormulaParser::makeDecision(const symbol_t& symbol, double exprValue, DataContext* context) {

}
