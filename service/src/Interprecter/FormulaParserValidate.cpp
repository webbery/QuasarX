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

// ========== FormulaParser 静态类型验证实现 ==========

FormulaParser::ExprType FormulaParser::inferExpressionType(
    const peg::Ast& ast, const Map<String, ArgType>& availableVars) {

    if (ast.name == "Number") {
        return ExprType::DOUBLE_SCALAR;
    }

    if (ast.name == "Identifier") {
        String varName(ast.token);
        auto it = availableVars.find(varName);
        if (it != availableVars.end()) {
            switch (it->second) {
                case ArgType::Double_Scalar:
                case ArgType::Double_Deprecated:
                    return ExprType::DOUBLE_SCALAR;
                case ArgType::Double_TimeSeries:
                    return ExprType::DOUBLE_TIMESERIES;
                case ArgType::Integer_Scalar:
                    return ExprType::INTEGER_SCALAR;
                case ArgType::Integer_TimeSeries:
                    return ExprType::INTEGER_TIMESERIES;
                case ArgType::Bool_Scalar:
                case ArgType::Bool_TimeSeries:
                    return ExprType::BOOL;
                default:
                    return ExprType::UNKNOWN;
            }
        }
        // 未找到的变量可能是截面函数或其他内置函数
        return ExprType::DOUBLE_TIMESERIES;  // 默认假设返回时间序列
    }

    if (ast.name == "Primary" && ast.nodes.size() > 1) {
        // 检查是否有 TimeOffset（如 [t], [t-1]）
        for (auto& node : ast.nodes) {
            if (node->name == "TimeOffset") {
                // 使用了时间索引，将时间序列转换为标量
                auto baseType = inferExpressionType(*ast.nodes.front(), availableVars);
                if (baseType == ExprType::DOUBLE_TIMESERIES ||
                    baseType == ExprType::INTEGER_TIMESERIES) {
                    return ExprType::DOUBLE_SCALAR;  // [t] 索引后变为标量
                }
                return baseType;
            }
        }
        // 没有索引，返回原始类型
        return inferExpressionType(*ast.nodes.front(), availableVars);
    }

    if (ast.name == "CompareExpr") {
        return ExprType::BOOL;  // 比较表达式返回布尔值
    }

    if (ast.name == "ArithExpr" || ast.name == "Term") {
        // 算术运算返回 double 标量
        return ExprType::DOUBLE_SCALAR;
    }

    if (ast.name == "AndExpr" || ast.name == "OrExpr" || ast.name == "NotExpr") {
        return ExprType::BOOL;
    }

    if (ast.name == "FunctionCall") {
        // 函数调用（包括截面函数）返回时间序列
        return ExprType::DOUBLE_TIMESERIES;
    }

    return ExprType::UNKNOWN;
}

bool FormulaParser::validateTimeOffset(const peg::Ast& ast, ExprType baseType) {
    if (baseType == ExprType::DOUBLE_TIMESERIES ||
        baseType == ExprType::INTEGER_TIMESERIES) {
        return true;  // 时间序列可以使用 [t] 索引
    }
    if (baseType == ExprType::DOUBLE_SCALAR ||
        baseType == ExprType::INTEGER_SCALAR) {
        _validationError = "Cannot use time index [t] on scalar value";
        return false;
    }
    return true;
}

bool FormulaParser::validateIdentifier(const peg::Ast& ast,
                                       const Map<String, ArgType>& availableVars,
                                       ExprType& outType) {
    String varName(ast.token);
    auto it = availableVars.find(varName);
    if (it == availableVars.end()) {
        // 变量不存在，可能是截面函数或其他内置函数，跳过检查
        outType = ExprType::UNKNOWN;
        return true;
    }

    switch (it->second) {
        case ArgType::Double_Scalar:
        case ArgType::Double_Deprecated:
            outType = ExprType::DOUBLE_SCALAR;
            break;
        case ArgType::Double_TimeSeries:
            outType = ExprType::DOUBLE_TIMESERIES;
            break;
        case ArgType::Integer_Scalar:
            outType = ExprType::INTEGER_SCALAR;
            break;
        case ArgType::Integer_TimeSeries:
            outType = ExprType::INTEGER_TIMESERIES;
            break;
        case ArgType::Bool_Scalar:
        case ArgType::Bool_TimeSeries:
            outType = ExprType::BOOL;
            break;
        default:
            outType = ExprType::UNKNOWN;
    }
    return true;
}

bool FormulaParser::validateComparison(const peg::Ast& ast,
                                       const Map<String, ArgType>& availableVars) {
    // CompareExpr 结构：left op right (nodes[0], nodes[1], nodes[2])
    if (ast.nodes.size() < 3) return true;

    auto leftType = inferExpressionType(*ast.nodes[0], availableVars);
    auto rightType = inferExpressionType(*ast.nodes[2], availableVars);
    String op(ast.nodes[1]->token);

    // 检查左边是否为时间序列（未使用 [t] 索引）
    if (leftType == ExprType::DOUBLE_TIMESERIES ||
        leftType == ExprType::INTEGER_TIMESERIES) {
        _validationError = fmt::format(
            "Type error in comparison '{}': left side is a time series. "
            "Use [t] or [t-1] to access specific value. Example: 'MA_5[t] {} ...'",
            op, op);
        return false;
    }

    // 检查右边是否为时间序列
    if (rightType == ExprType::DOUBLE_TIMESERIES ||
        rightType == ExprType::INTEGER_TIMESERIES) {
        _validationError = fmt::format(
            "Type error in comparison '{}': right side is a time series. "
            "Use [t] or [t-1] to access specific value. Example: '... {} MA_5[t]'",
            op, op);
        return false;
    }

    // 检查类型是否匹配（标量之间可以比较）
    if ((leftType == ExprType::DOUBLE_SCALAR || leftType == ExprType::INTEGER_SCALAR) &&
        (rightType == ExprType::DOUBLE_SCALAR || rightType == ExprType::INTEGER_SCALAR)) {
        return true;
    }

    // 其他情况
    if (leftType == ExprType::UNKNOWN || rightType == ExprType::UNKNOWN) {
        return true;  // 无法推断类型时，假设正确
    }

    _validationError = fmt::format(
        "Type mismatch in comparison '{}': incompatible types", op);
    return false;
}

bool FormulaParser::validateArithmetic(const peg::Ast& ast,
                                       const Map<String, ArgType>& availableVars) {
    // 检查算术表达式中的操作数
    for (auto& node : ast.nodes) {
        if (node->name == "Primary" || node->name == "Term" || node->name == "ArithExpr") {
            auto type = inferExpressionType(*node, availableVars);
            if (type == ExprType::DOUBLE_TIMESERIES ||
                type == ExprType::INTEGER_TIMESERIES) {
                _validationError = fmt::format(
                    "Type error in arithmetic: time series cannot be used directly. "
                    "Use [t] or [t-1] index. Example: 'MA_5[t] - MA_15[t]'");
                return false;
            }
        }
    }
    return true;
}

bool FormulaParser::validate(const Map<String, ArgType>& availableVars) {
    if (!_ast) return false;
    _validationError.clear();

    // 递归遍历 AST 进行类型检查
    std::function<bool(const peg::Ast&)> visit = [&](const peg::Ast& node) -> bool {
        if (node.name == "CompareExpr") {
            if (!validateComparison(node, availableVars)) {
                return false;
            }
        }
        else if (node.name == "ArithExpr") {
            if (!validateArithmetic(node, availableVars)) {
                return false;
            }
        }
        else if (node.name == "Primary" && node.nodes.size() > 1) {
            // 检查 TimeOffset
            for (size_t i = 1; i < node.nodes.size(); ++i) {
                if (node.nodes[i]->name == "TimeOffset") {
                    auto baseType = inferExpressionType(*node.nodes.front(), availableVars);
                    if (!validateTimeOffset(*node.nodes[i], baseType)) {
                        return false;
                    }
                }
            }
        }

        // 递归检查子节点
        for (auto& child : node.nodes) {
            if (!visit(*child)) {
                return false;
            }
        }
        return true;
    };

    return visit(*_ast);
}
