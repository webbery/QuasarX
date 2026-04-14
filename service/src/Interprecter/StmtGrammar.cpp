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
        NotExpr         <- 'not' NotExpr / '!' NotExpr / CompareExpr { no_ast_opt }
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

        # 标识符和数字（排除关键字）
        Identifier      <- !('not' / 'and' / 'or' / 'true' / 'false') < [a-zA-Z_][a-zA-Z_0-9]* >
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
            } else if constexpr (std::is_same_v<T, Vector<double>>) {
                THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '>' operator. "
                               "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] > 0'. "
                               "This error should have been caught during validation.");
            } else {
                THROW_EXCEPTION("Runtime type error: Comparison '>' not supported for type: {}",
                               typeid(T).name());
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
            } else if constexpr (std::is_same_v<T, Vector<double>>) {
                THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '<' operator. "
                               "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] < 0'.");
            } else {
                THROW_EXCEPTION("Runtime type error: Comparison '<' not supported for type: {}",
                               typeid(T).name());
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
            } else if constexpr (std::is_same_v<T, Vector<double>>) {
                THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '==' operator. "
                               "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] == 0'.");
            } else {
                THROW_EXCEPTION("Runtime type error: Comparison '==' not supported for type: {}",
                               typeid(T).name());
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
            } else if constexpr (std::is_same_v<T, Vector<double>>) {
                THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '!=' operator. "
                               "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] != 0'.");
            } else {
                THROW_EXCEPTION("Runtime type error: Comparison '!=' not supported for type: {}",
                               typeid(T).name());
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
            } else if constexpr (std::is_same_v<T, Vector<double>>) {
                THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '>=' operator. "
                               "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] >= 0'.");
            } else {
                THROW_EXCEPTION("Runtime type error: Comparison '>=' not supported for type: {}",
                               typeid(T).name());
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
            } else if constexpr (std::is_same_v<T, Vector<double>>) {
                THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '<=' operator. "
                               "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] <= 0'.");
            } else {
                THROW_EXCEPTION("Runtime type error: Comparison '<=' not supported for type: {}",
                               typeid(T).name());
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
