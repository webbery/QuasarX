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

context_t FormulaParser::evalNotExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (ast.nodes.size() == 2) {
        auto value = evalNode(symbol, *ast.nodes[1], context);
        return !statement::check_bool(value);
    } else {
        return evalNode(symbol, *ast.nodes[0], context);
    }
}

context_t FormulaParser::evalNode(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (statement::evalMap.count(ast.name) == 0) {
        INFO("ast node `{}` not found", ast.name);
        return false;
    }

    return (this->*(statement::evalMap[ast.name]))(symbol, ast, context);
}

context_t FormulaParser::evalFunctionCall(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto funcName = String(ast.nodes[0]->token);

    // 如果是截面函数，在 envokeMixedCase 中已经预计算，直接从 context 读取
    if (isCrossSectionFunction(funcName)) {
        if (_varToNodeId.count(funcName)) {
            String nodeId = _varToNodeId[funcName];
            auto it = _csGraph.nodes.find(nodeId);
            if (it != _csGraph.nodes.end() && it->second.computed && it->second.outputs.count(symbol)) {
                DEBUG_INFO("[evalFunctionCall] Using cached result for {}, symbol={}, returning value",
                           funcName, get_symbol(symbol));
                return it->second.outputs.at(symbol);
            } else {
                DEBUG_INFO("[evalFunctionCall] {} cache miss: node_found={}, computed={}, has_output={}",
                           funcName,
                           it != _csGraph.nodes.end(),
                           it != _csGraph.nodes.end() ? it->second.computed : false,
                           it != _csGraph.nodes.end() ? it->second.outputs.count(symbol) : 0);
            }
        } else {
            DEBUG_INFO("[evalFunctionCall] {} not found in _varToNodeId", funcName);
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
        result = statement::arithmeticMap[op](result, operand);
    }

    return result;
}
