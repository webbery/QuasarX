#include "Interprecter/Stmt.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "peglib.h"
#include "server.h"
#include <boost/algorithm/string/join.hpp>
#include <cstdint>
#include <functional>
#include <variant>
#include <stack>
#include <queue>
#include <algorithm>
#include <cmath>
#include <numeric>

// ========== FormulaParser 静态类型验证实现 ==========

namespace {

// 公式里合法出现、但不是「变量」的标识符：内置函数名。
// validateIdentifier 只被用于校验变量引用，遇到这些名字必须放行——
// 表达式 zscore(close, 20) 里的 zscore 是截面函数，close 才是变量。
// 该名单与 FormulaParserEval.cpp 中 evalFunctionCall 实际 dispatch 的分支
// 保持一致，外加 isCrossSectionFunction() 覆盖的截面函数。
const UnorderedSet<String>& builtinFunctionNames() {
    static const UnorderedSet<String> names{
        // evalFunctionCall 中的一元/二元数学函数
        "abs", "exp", "log", "sqrt", "sigmoid", "min", "max",
        "argmax", "count", "MA",
        "rolling_topk", "rolling_topk_idx",
        // 截面函数（isCrossSectionFunction）
        "topk", "bottomk", "rank", "zscore", "pct",
        "cs_count", "cs_size",
    };
    return names;
}

// availableVars 里既有 {symbol}.{var} 全名，也有 validate(availableVars, symbols)
// 注入的短名；单参数版本没有短名，校验列索引时两种形式都要认。
// 带 "." 前缀锚定，避免 "probs_0" 误命中 "{symbol}.xgb_probs_0" 的尾部。
bool varExists(const Map<String, ArgType>& availableVars, const String& name) {
    if (availableVars.find(name) != availableVars.end()) {
        return true;
    }
    String suffix = "." + name;
    for (const auto& kv : availableVars) {
        const String& k = kv.first;
        if (k.size() > suffix.size() &&
            k.compare(k.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

FormulaParser::ExprType FormulaParser::inferExpressionType(
    const peg::Ast& ast, const Map<String, ArgType>& availableVars) {

    if (ast.name == "Number") {
        return ExprType::DOUBLE_SCALAR;
    }

    if (ast.name == "BoolLiteral") {
        return ExprType::BOOL;
    }

    if (ast.name == "Identifier") {
        String varName(ast.token);
        auto it = availableVars.find(varName);
        if (it != availableVars.end()) {
            switch (it->second) {
                case ArgType::Double_Scalar:
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
        // 两种下标都会把序列塌缩成标量：[t] 取历史某根 bar，[N] 取某一列的最新值
        for (auto& node : ast.nodes) {
            if (node->name == "TimeIndex" || node->name == "ColumnIndex") {
                if (node->name == "ColumnIndex") {
                    return ExprType::DOUBLE_SCALAR;
                }
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

    if (ast.name == "ArithExpr" || ast.name == "Term" || ast.name == "Unary") {
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
    return validateIdentifierName(String(ast.token), availableVars, outType);
}

bool FormulaParser::resolveColumnName(const peg::Ast& primary, String& outColumnName) const {
    // xgb_probs[0] 的 AST 是 Primary[Identifier "xgb_probs", ColumnIndex "0"]，
    // 真实 key 由语法唯一确定：xgb_probs_0。不做「先当变量名查、查不到再猜 _N」。
    if (primary.nodes.size() < 2 || primary.nodes.front()->name != "Identifier") {
        return false;
    }
    for (size_t i = 1; i < primary.nodes.size(); ++i) {
        if (primary.nodes[i]->name == "ColumnIndex") {
            // token 是 string_view，先转成 String 再拼，跟本文件其他地方的写法一致
            String base(primary.nodes.front()->token);
            String index(primary.nodes[i]->token);
            outColumnName = base + "_" + index;
            return true;
        }
    }
    return false;
}

bool FormulaParser::validateIdentifierName(const String& varName,
                                           const Map<String, ArgType>& availableVars,
                                           ExprType& outType) {
    auto it = availableVars.find(varName);
    if (it == availableVars.end()) {
        // 内置函数名不是变量引用，放行
        if (builtinFunctionNames().count(varName)) {
            outType = ExprType::UNKNOWN;
            return true;
        }

        // 未知变量必须报错，不能放行。
        // 放行时运行期只会拿到 NaN，而 NaN 参与比较恒为 false：SignalNode 全程
        // HOLD，回测正常跑完但 0 笔交易，指标全零，日志里只有一条 debug 级
        // 「key not found」。拼错变量名（含 FormulaNode 的 label 写错导致的
        // 上下游 key 对不上）曾经就是这样静默失效的。
        //
        // availableVars 里既有 {symbol}.{var} 全名，也有 validate() 第二参数
        // 注入的短名。报错时把同前缀的候选列出来，直接指向「是不是 label 写错了」。
        Vector<String> candidates;
        String prefix;
        size_t dot = varName.find('.');
        if (dot != String::npos) prefix = varName.substr(0, dot + 1);
        for (auto& kv : availableVars) {
            if (prefix.empty() || kv.first.compare(0, prefix.size(), prefix) == 0) {
                candidates.push_back(kv.first);
            }
        }
        std::sort(candidates.begin(), candidates.end());
        if (candidates.size() > 12) candidates.resize(12);

        _validationError = fmt::format(
            "Unknown variable '{}' in formula. Upstream nodes do not publish this key — "
            "check the spelling, and for FormulaNode make sure its 'label' is an ASCII "
            "identifier that matches the name used downstream. Available: [{}]",
            varName, boost::algorithm::join(candidates, ", "));
        outType = ExprType::UNKNOWN;
        return false;
    }

    switch (it->second) {
        case ArgType::Double_Scalar:
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
        if (node->name == "Primary" || node->name == "Term" || node->name == "ArithExpr" || node->name == "Unary") {
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
            // 检查 TimeIndex
            for (size_t i = 1; i < node.nodes.size(); ++i) {
                if (node.nodes[i]->name == "TimeIndex") {
                    auto baseType = inferExpressionType(*node.nodes.front(), availableVars);
                    if (!validateTimeOffset(*node.nodes[i], baseType)) {
                        return false;
                    }
                }
            }
        }

        // 列索引 xgb_probs[0]：Identifier 只是列名前缀，要校验的是展开后的 xgb_probs_0。
        // 报未知变量时也报展开后的名字——用户写的是 [0]，回一句 "xgb_probs_0 not found"
        // 比 "xgb_probs not found" 更能指出是第几列不存在。
        String columnName;
        bool hasColumnIndex = node.name == "Primary" &&
                              resolveColumnName(node, columnName);
        if (hasColumnIndex && !varExists(availableVars, columnName)) {
            ExprType ignored = ExprType::UNKNOWN;
            return validateIdentifierName(columnName, availableVars, ignored);
        }

        // 未知变量检查。放在遍历器里而不是单独调用 validateIdentifier，是因为
        // 只有遍历器知道父节点，才能区分「变量引用」和同样叫 Identifier 的：
        //   FunctionCall    → nodes[0] 是函数名（zscore(...) 里的 zscore）
        //   AssignmentStmt  → nodes[0] 是赋值目标（x = ... 里的 x）
        //   Trailer         → 整个子节点都是成员名（a.b 里的 b）
        // 这三类都不是变量引用，送进 validateIdentifier 会被误判成拼错的变量。
        size_t firstVarIdx = 0;
        if (node.name == "FunctionCall" || node.name == "AssignmentStmt") {
            firstVarIdx = node.nodes.empty() ? 0 : 1;
        } else if (node.name == "Trailer") {
            firstVarIdx = node.nodes.size();  // 整个 Trailer 都不检查
        } else if (hasColumnIndex) {
            firstVarIdx = 1;  // nodes[0] 的列名已按 columnName 校验过
        }
        for (size_t i = firstVarIdx; i < node.nodes.size(); ++i) {
            const auto& child = node.nodes[i];
            if (child->name == "Identifier") {
                ExprType ignored = ExprType::UNKNOWN;
                if (!validateIdentifier(*child, availableVars, ignored)) {
                    return false;
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

bool FormulaParser::validate(const Map<String, ArgType>& availableVars,
                             const Vector<symbol_t>& symbols) {
    if (!_ast) return false;
    _validationError.clear();

    // 向后兼容:空 symbols 直接复用旧逻辑(此时没有任何 short-name 别名可以注入)
    if (symbols.empty()) {
        return validate(availableVars);
    }

    // 构造 augmented availableVars: 保留 full key,同时按 symbols 注入 short key。
    // 例: full key "sz.900007.close" + symbol sz.900007 → short key "close"。
    // 同名 short key 多 symbol 命中时,首个生效(实际多 symbol 上游类型一致,无副作用)。
    Map<String, ArgType> aug = availableVars;
    for (const auto& sym : symbols) {
        String prefix = get_symbol(sym) + ".";
        size_t plen = prefix.size();
        for (const auto& kv : availableVars) {
            const String& k = kv.first;
            if (k.size() <= plen || k.compare(0, plen, prefix) != 0) continue;
            String shortKey = k.substr(plen);
            // 已存在则跳过(首个胜出)
            aug.emplace(std::move(shortKey), kv.second);
        }
    }

    // 调用单参数版本(递归 visit 逻辑保持不变)
    return validate(aug);
}
