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

#define INTRINSIC_TOPK      "topk"
#define INTRINSIC_BOTTOMK   "bottomk"
#define INTRINSIC_RANK      "rank"
#define INTRINSIC_ZSCORE    "zscore"
#define INTRINSIC_PERCENTILE "pct"

// ========== FormulaParser eval 方法实现 ==========

context_t FormulaParser::eval(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    return evalNode(symbol, ast, context);
}

context_t FormulaParser::evalNumber(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    return ast.token_to_number<double>();
}

context_t FormulaParser::evalBoolLiteral(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    // ast.token 是 "true"/"false"/"True"/"False"
    String token(ast.token);
    return (token == "true" || token == "True");
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
    auto result = statement::comparationMap[op](left, right);
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
        return evalNode(symbol, *ast.nodes[1], context);
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

    if (!context.exist(key)) {
        // 输出 DataContext 中所有可用的键，帮助调试
        String availableKeys;
        // 注意：DataContext 没有暴露 _outputs 的迭代接口，这里只能提示
        WARN("FormulaParser: key '{}' not found for symbol '{}', expression may reference a non-existent feature", key, name);
        WARN("FormulaParser: checking for '{}.ma_short' or '{}.ma_long'?", name, name);
        return 0.0;
    }

    auto& vec = context.get<Vector<double>>(key);
    if (vec.empty()) {
        WARN("FormulaParser: key '{}' exists but vector is empty for symbol '{}'", key, name);
        return 0.0;
    }

    int idx = (int)vec.size() - 1 + time_offset;
    if (idx >= 0 && idx < (int)vec.size()) {
        return vec[idx];
    } else {
        WARN("getHistoricalValue - index out of range, idx={}, size={}, key={}", idx, vec.size(), key);
        return vec.back();
    }
}

context_t FormulaParser::evalOrExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto left = evalNode(symbol, *ast.nodes[0], context);
    if (statement::check_bool(left)) return true;

    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        auto right = evalNode(symbol, *ast.nodes[i], context);
        if (statement::check_bool(right)) return true;
    }
    return false;
}

context_t FormulaParser::evalAndExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto left = evalNode(symbol, *ast.nodes[0], context);
    if (statement::check_bool(left) == false) return false;

    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        auto right = evalNode(symbol, *ast.nodes[i], context);
        if (statement::check_bool(right) == false) return false;
    }
    return true;
}

// context_t FormulaParser::evalNotExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
//     if (ast.nodes.size() == 2) {
//         auto value = evalNode(symbol, *ast.nodes[1], context);
//         return !statement::check_bool(value);
//     } else {
//         return evalNode(symbol, *ast.nodes[0], context);
//     }
// }

context_t FormulaParser::evalNotPrefix(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto value = evalNode(symbol, *ast.nodes[0], context);
    return !statement::check_bool(value);
}

context_t FormulaParser::evalNode(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (statement::evalMap.count(ast.name) == 0) {
        INFO("ast node `{}` not found", ast.name);
        return false;
    }
    return (this->*(statement::evalMap[ast.name]))(symbol, ast, context);
}

// 辅助：从 context_t 提取 double 标量
static double ctxToDouble(const context_t& v) {
    if (auto* p = std::get_if<double>(&v)) return *p;
    if (auto* p = std::get_if<bool>(&v)) return *p ? 1.0 : 0.0;
    if (auto* p = std::get_if<uint64_t>(&v)) return static_cast<double>(*p);
    if (auto* p = std::get_if<Vector<double>>(&v)) return p->empty() ? 0.0 : p->back();
    return 0.0;
}

// 辅助：对 context_t 逐元素应用一元数学函数（支持 double 标量和 Vector<double>）
static context_t applyUnaryMath(const context_t& arg, double(*fn)(double)) {
    if (auto* p = std::get_if<Vector<double>>(&arg)) {
        Vector<double> result(p->size());
        for (size_t i = 0; i < p->size(); ++i)
            result[i] = fn((*p)[i]);
        return result;
    }
    return fn(ctxToDouble(arg));
}

static double sigmoid_fn(double x) {
    return 1.0 / (1.0 + std::exp(-x));
}

context_t FormulaParser::evalFunctionCall(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto funcName = String(ast.nodes[0]->token);

    // ── 内置数学函数 ──────────────────────────────────────────────
    // 一元: abs, exp, log, sqrt, sigmoid
    // 二元: min, max
    if (ast.nodes.size() >= 2) {
        auto& argsNode = ast.nodes[1];
        if (argsNode->name == "Arguments") {
            if (funcName == "abs" && argsNode->nodes.size() == 1) {
                auto arg = evalNode(symbol, *argsNode->nodes[0], context);
                return applyUnaryMath(arg, std::abs);
            }
            if (funcName == "exp" && argsNode->nodes.size() == 1) {
                auto arg = evalNode(symbol, *argsNode->nodes[0], context);
                return applyUnaryMath(arg, std::exp);
            }
            if (funcName == "log" && argsNode->nodes.size() == 1) {
                auto arg = evalNode(symbol, *argsNode->nodes[0], context);
                return applyUnaryMath(arg, std::log);
            }
            if (funcName == "sqrt" && argsNode->nodes.size() == 1) {
                auto arg = evalNode(symbol, *argsNode->nodes[0], context);
                return applyUnaryMath(arg, std::sqrt);
            }
            if (funcName == "sigmoid" && argsNode->nodes.size() == 1) {
                auto arg = evalNode(symbol, *argsNode->nodes[0], context);
                return applyUnaryMath(arg, sigmoid_fn);
            }
            if (funcName == "min" && argsNode->nodes.size() == 2) {
                auto a = evalNode(symbol, *argsNode->nodes[0], context);
                auto b = evalNode(symbol, *argsNode->nodes[1], context);
                // 如果任一参数是 Vector，逐元素取 min
                if (auto* va = std::get_if<Vector<double>>(&a)) {
                    double bv = ctxToDouble(b);
                    Vector<double> result(va->size());
                    for (size_t i = 0; i < va->size(); ++i)
                        result[i] = std::min((*va)[i], bv);
                    return result;
                }
                if (auto* vb = std::get_if<Vector<double>>(&b)) {
                    double av = ctxToDouble(a);
                    Vector<double> result(vb->size());
                    for (size_t i = 0; i < vb->size(); ++i)
                        result[i] = std::min(av, (*vb)[i]);
                    return result;
                }
                return std::min(ctxToDouble(a), ctxToDouble(b));
            }
            if (funcName == "max" && argsNode->nodes.size() == 2) {
                auto a = evalNode(symbol, *argsNode->nodes[0], context);
                auto b = evalNode(symbol, *argsNode->nodes[1], context);
                if (auto* va = std::get_if<Vector<double>>(&a)) {
                    double bv = ctxToDouble(b);
                    Vector<double> result(va->size());
                    for (size_t i = 0; i < va->size(); ++i)
                        result[i] = std::max((*va)[i], bv);
                    return result;
                }
                if (auto* vb = std::get_if<Vector<double>>(&b)) {
                    double av = ctxToDouble(a);
                    Vector<double> result(vb->size());
                    for (size_t i = 0; i < vb->size(); ++i)
                        result[i] = std::max(av, (*vb)[i]);
                    return result;
                }
                return std::max(ctxToDouble(a), ctxToDouble(b));
            }
        }
    }

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
    if (!context->exist(key)) {
        WARN("getVariableValue: key '{}' not found for symbol '{}'", key, str);
        return 0.0;
    }
    return context->get(key);
}

context_t FormulaParser::evalArithmetic(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto result = evalNode(symbol, *ast.nodes[0], context);
    for (size_t i = 1; i < ast.nodes.size(); i += 2) {
        char op = ast.nodes[i]->token[0];
        auto operand = evalNode(symbol, *ast.nodes[i + 1], context);
        result = statement::arithmeticMap[op](result, operand);
    }

    return result;
}

// ========== computeNumeric 实现 ==========

Map<symbol_t, double> FormulaParser::computeNumeric(const Vector<symbol_t>& symbols,
                                                     const Set<String>& variantNames,
                                                     DataContext& context) {
    Map<symbol_t, double> results;

    if (!_ast) {
        return results;
    }

    // 预处理截面函数（复用现有逻辑）
    if (hasCrossSectionFunctions(*_ast)) {
        precomputeCrossSectionFunctions(symbols, context);
    }

    // 逐 symbol 计算数值
    for (auto& sym : symbols) {
        context_t val = eval(sym, *_ast, context);
        double numericValue = 0.0;
        std::visit([&numericValue](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double>) {
                numericValue = v;
            } else if constexpr (std::is_same_v<T, Vector<double>>) {
                numericValue = v.empty() ? 0.0 : v.back();
            } else if constexpr (std::is_same_v<T, bool>) {
                numericValue = v ? 1.0 : 0.0;
            }
        }, val);
        results[sym] = numericValue;
    }
    return results;
}
