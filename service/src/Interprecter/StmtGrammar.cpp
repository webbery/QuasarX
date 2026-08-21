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

namespace statement{
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
        NotExpr         <- NotPrefix / CompareExpr
        NotPrefix       <- ('not' / '!') NotExpr { no_ast_opt }
        CompareExpr     <- ArithExpr (CompareOp ArithExpr)*
        ArithExpr       <- Term (AddOp Term)*
        Term            <- Unary (MulOp Unary)*
        Unary           <- UnaryOp Unary / Primary { no_ast_opt }
        UnaryOp         <- '-'
        Primary         <- Atom (Trailer)*
        Atom            <- Number / String / BoolLiteral / FunctionCall / ListExpr / Identifier / '(' Expression ')'

        # 时间序列访问
        Trailer         <- '.' Identifier / '(' Arguments? ')' / '[' TimeOffset ']'
        TimeOffset      <- < 't' '-' [0-9]+ > / < 't' > / < [0-9]+ >

        # 函数调用
        FunctionCall    <- Identifier '(' Arguments? ')'
        Arguments       <- Expression (',' Expression)*

        # 数据结构
        ListExpr        <- '[' Expression (',' Expression)* ']'

        # 布尔字面量（支持大小写）
        BoolLiteral     <- < 'true' > / < 'false' > / < 'True' > / < 'False' >

        # 标识符和数字（排除关键字）
        Identifier      <- !('not' / 'and' / 'or' / 'true' / 'false' / 'True' / 'False') < [a-zA-Z_][a-zA-Z_0-9]* >
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

// 辅助：统一将 context_t 转为 double（支持 double/uint64_t/bool/String 等）
// PEG 解析整数字面量（如 0, 1）可能返回 uint64_t，比较结果返回 bool
// 文件级函数，供 comparationMap 和 arithmeticMap 共用
static double ctxToDoubleArith(const context_t& v) {
    if (auto* p = std::get_if<double>(&v))     return *p;
    if (auto* p = std::get_if<bool>(&v))       return *p ? 1.0 : 0.0;
    if (auto* p = std::get_if<uint64_t>(&v))   return static_cast<double>(*p);
    if (auto* p = std::get_if<Vector<double>>(&v)) return p->empty() ? 0.0 : p->back();
    if (auto* p = std::get_if<String>(&v)) {
        try { return std::stod(*p); } catch (...) { return 0.0; }
    }
    return 0.0;
}

Map<String, std::function<bool (const context_t& , const context_t& )>>& comparationMap() {
    static Map<String, std::function<bool (const context_t& , const context_t& )>> m{
    {">", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l > ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '>' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] > 0'. "
                           "This error should have been caught during validation.");
        }
        THROW_EXCEPTION("Runtime type error: Comparison '>' not supported for type index: {}", left.index());
    }},
    {"<", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l < ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '<' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] < 0'.");
        }
        THROW_EXCEPTION("Runtime type error: Comparison '<' not supported for type index: {}", left.index());
    }},
    {"==", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l == ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '==' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] == 0'.");
        }
        // 支持 bool/uint64_t 等非 double 标量类型的比较
        return ctxToDoubleArith(left) == ctxToDoubleArith(right);
    }},
    {"!=", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l != ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '!=' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] != 0'.");
        }
        return ctxToDoubleArith(left) != ctxToDoubleArith(right);
    }},
    {">=", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l >= ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '>=' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] >= 0'.");
        }
        return ctxToDoubleArith(left) >= ctxToDoubleArith(right);
    }},
    {"<=", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l <= ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '<=' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] <= 0'.");
        }
        return ctxToDoubleArith(left) <= ctxToDoubleArith(right);
    }},
};
    return m;
}

Map<char, std::function<context_t(const context_t& , const context_t&)>>& arithmeticMap() {
    static Map<char, std::function<context_t(const context_t& , const context_t&)>> m{
    {'+', [](const context_t& left, const context_t& right) {
        return ctxToDoubleArith(left) + ctxToDoubleArith(right);
    }},
    {'-', [](const context_t& left, const context_t& right) {
        return ctxToDoubleArith(left) - ctxToDoubleArith(right);
    }},
    {'*', [](const context_t& left, const context_t& right) {
        return ctxToDoubleArith(left) * ctxToDoubleArith(right);
    }},
    {'/', [](const context_t& left, const context_t& right) {
        double r = ctxToDoubleArith(right);
        if (std::abs(r) < 1e-10 || std::isnan(r)) {
            WARN("Division by zero / NaN detected for symbol: {}", r);
            return std::nan("nan");
        } else { [[likely]]
            return ctxToDoubleArith(left) / r;
        }
    }},
};
    return m;
}

Map<String, EvalPtr>& evalMap() {
    static Map<String, EvalPtr> m{
    {"Number", &FormulaParser::evalNumber},
    {"BoolLiteral", &FormulaParser::evalBoolLiteral},
    {"Identifier", &FormulaParser::evalIdentifier},
    {"CompareExpr", &FormulaParser::evalComparison},
    {"FunctionCall", &FormulaParser::evalFunctionCall},
    {"Term", &FormulaParser::evalTerm},
    {"Unary", &FormulaParser::evalUnary},
    {"Program", &FormulaParser::evalProgram},
    {"Statement", &FormulaParser::evalStatement},
    {"AndExpr", &FormulaParser::evalAndExpr},
    {"OrExpr", &FormulaParser::evalOrExpr},
    {"NotExpr", &FormulaParser::evalNotExpr},
    {"NotPrefix", &FormulaParser::evalNotPrefix},
    {"Primary", &FormulaParser::evalPrimary},
    {"ArithExpr", &FormulaParser::evalArithmetic},
    {"Expression", &FormulaParser::evalExpression},
    {"ExpressionStmt", &FormulaParser::evalStatement}
};
    return m;
}

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
