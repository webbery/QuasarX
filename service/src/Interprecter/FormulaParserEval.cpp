#include "Interprecter/Stmt.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "Algorithms/RollingTopK.h"
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
    auto result = statement::comparationMap()[op](left, right);
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

context_t FormulaParser::evalUnary(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    // Unary <- UnaryOp Unary / Primary
    if (ast.nodes.size() == 2 && ast.nodes[0]->name == "UnaryOp") {
        auto val = evalNode(symbol, *ast.nodes[1], context);
        if (auto* d = std::get_if<double>(&val)) {
            return -(*d);
        }
        if (auto* v = std::get_if<Vector<double>>(&val)) {
            Vector<double> result(v->size());
            for (size_t i = 0; i < v->size(); ++i)
                result[i] = -(*v)[i];
            return result;
        }
        return -std::get<double>(val);
    }
    // Fallback: Primary
    return evalNode(symbol, *ast.nodes[0], context);
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

context_t FormulaParser::evalExpression(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    // Expression <- OrExpr，直接转发到子节点
    if (!ast.nodes.empty()) {
        return evalNode(symbol, *ast.nodes[0], context);
    }
    return 0.0;
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
    // 标量 passthrough：数组展开后返回的 double 再经过 [t] 时会到这里
    else if (std::holds_alternative<double>(base)) {
        return std::get<double>(base);
    }

    String var_name = std::get<String>(base);
    auto name = get_symbol(symbol);
    String key = name + "." + var_name;

    if (!context.exist(key)) {
        // 数组展开: name[N] → name_N (如 xgb_probs[0] → xgb_probs_0)
        if (time_offset >= 0) {
            String arrayKey = name + "." + var_name + "_" + std::to_string(time_offset);
            if (context.exist(arrayKey)) {
                auto& vec = context.get<Vector<double>>(arrayKey);
                if (!vec.empty()) {
                    int idx = (int)vec.size() - 1;  // 取最新值
                    return vec[idx];
                }
            }
        }
        DEBUG_INFO("FormulaParser: key '{}' not found for symbol '{}'", key, name);
        return 0.0;
    }

    auto& vec = context.get<Vector<double>>(key);
    if (vec.empty()) {
        return 0.0;
    }

    int idx = (int)vec.size() - 1 + time_offset;
    if (idx >= 0 && idx < (int)vec.size()) {
        return vec[idx];
    } else {
        return vec.front();
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
    return evalNode(symbol, *ast.nodes[0], context);
}

context_t FormulaParser::evalNotPrefix(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    auto value = evalNode(symbol, *ast.nodes[0], context);
    return !statement::check_bool(value);
}

context_t FormulaParser::evalNode(const symbol_t& symbol, const peg::Ast& ast, DataContext& context) {
    if (statement::evalMap().count(ast.name) == 0) {
        INFO("ast node `{}` not found", ast.name);
        return false;
    }
    return (this->*(statement::evalMap()[ast.name]))(symbol, ast, context);
}

// 辅助：从 context_t 提取 double 标量
// 注意：PEG 解析字面量 `1` 时可能返回 size_t / uint64_t 而非 double，
// 早期只支持 uint64_t 导致 args[1] = "1" 被 ctxToDouble 默认为 0，
// 进而 count 等 intrinsic 的 sign 参数错误。
// 现扩展支持全部 context_t 可能类型，包括 String(数字字符串) 防御。
static double ctxToDouble(const context_t& v) {
    if (auto* p = std::get_if<double>(&v))     return *p;
    if (auto* p = std::get_if<bool>(&v))       return *p ? 1.0 : 0.0;
    if (auto* p = std::get_if<uint64_t>(&v))   return static_cast<double>(*p);
    if (auto* p = std::get_if<Vector<float>>(&v))     return p->empty() ? 0.0 : static_cast<double>(p->back());
    if (auto* p = std::get_if<Vector<double>>(&v))    return p->empty() ? 0.0 : p->back();
    if (auto* p = std::get_if<Vector<uint64_t>>(&v))  return p->empty() ? 0.0 : static_cast<double>(p->back());
    if (auto* p = std::get_if<String>(&v)) {
        // 数字字符串：解析返回，解析失败 0.0
        try { return std::stod(*p); } catch (...) { return 0.0; }
    }
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
        
        // 辅助 lambda：获取参数列表
        // PEG 对单参数 Arguments 会优化，直接返回参数节点而非 Arguments 节点
        auto getArgs = [&]() -> std::vector<const peg::Ast*> {
            if (argsNode->name == "Arguments") {
                std::vector<const peg::Ast*> result;
                for (auto& n : argsNode->nodes) {
                    result.push_back(n.get());
                }
                return result;
            } else {
                // 单参数优化：argsNode 本身就是参数（Primary/Number 等）
                return {argsNode.get()};
            }
        };
        
        auto args = getArgs();
        
        // 一元函数
        if (args.size() == 1) {
            if (funcName == "argmax") {
                auto arg = evalNode(symbol, *args[0], context);
                // Vector<double>: 返回最大值索引 (0-based)
                if (auto* vec = std::get_if<Vector<double>>(&arg)) {
                    if (vec->empty()) return -1.0;
                    size_t best_idx = 0;
                    double best_val = (*vec)[0];
                    for (size_t i = 1; i < vec->size(); ++i) {
                        if ((*vec)[i] > best_val) {
                            best_val = (*vec)[i];
                            best_idx = i;
                        }
                    }
                    return static_cast<double>(best_idx);
                }
                // 单个 double: 唯一索引即 0
                return 0.0;
            }
            if (funcName == "rolling_topk") {
                // 签名: rolling_topk(values, k)  → Vector<double> (top-K values, desc)
                auto arg = evalNode(symbol, *args[0], context);
                int k = static_cast<int>(ctxToDouble(evalNode(symbol, *args[1], context)));
                if (auto* vec = std::get_if<Vector<double>>(&arg)) {
                    Eigen::VectorXd eig(vec->size());
                    for (size_t i = 0; i < vec->size(); ++i) eig(i) = (*vec)[i];
                    auto result = Alg::rolling_topk(eig, k, /*desc=*/true);
                    Vector<double> out(result._values.size());
                    for (Eigen::Index i = 0; i < result._values.size(); ++i) {
                        out[i] = result._values(i);
                    }
                    return out;
                }
                // 单个 double + k: 返回 K 个该值
                double v = ctxToDouble(arg);
                Vector<double> out(std::max(0, k), v);
                return out;
            }
            if (funcName == "rolling_topk_idx") {
                // 签名: rolling_topk_idx(values, k) → Vector<double> (top-K 索引, 按值降序排列)
                auto arg = evalNode(symbol, *args[0], context);
                int k = static_cast<int>(ctxToDouble(evalNode(symbol, *args[1], context)));
                if (auto* vec = std::get_if<Vector<double>>(&arg)) {
                    Eigen::VectorXd eig(vec->size());
                    for (size_t i = 0; i < vec->size(); ++i) eig(i) = (*vec)[i];
                    auto result = Alg::rolling_topk(eig, k, /*desc=*/true);
                    Vector<double> out(result._indices.size());
                    for (Eigen::Index i = 0; i < result._indices.size(); ++i) {
                        out[i] = static_cast<double>(result._indices(i));
                    }
                    return out;
                }
                // 单个 double + k: 返回 [0, 1, ..., K-1]
                Vector<double> out(std::max(0, k));
                for (int i = 0; i < k; ++i) out[i] = i;
                return out;
            }
            if (funcName == "count") {
                // 签名: count(vec, sign) → double
                //   sign > 0:  统计 v > 0  的元素数 (positiveCount)
                //   sign < 0:  统计 v < 0  的元素数 (negativeCount)
                //   sign == 0: 统计 v == 0 的元素数 (zeroCount)
                //
                // 语义来源: v14 consistency_threshold 用 pos_count / neg_count / size
                //         dominant_ratio = max(pos, neg) / (pos + neg)
                //
                // 命名遵循 naming_reflects_semantics.md：positiveCount 仅含 v > 0，
                // negativeCount 仅含 v < 0，零既不属于正也不属于负。
                auto arg0 = evalNode(symbol, *args[0], context);
                double sign = (args.size() >= 2)
                              ? ctxToDouble(evalNode(symbol, *args[1], context))
                              : 1.0;

                if (auto* vec = std::get_if<Vector<double>>(&arg0)) {
                    size_t positiveCount = 0, negativeCount = 0, zeroCount = 0;
                    for (double v : *vec) {
                        if (v > 0)       ++positiveCount;
                        else if (v < 0)  ++negativeCount;
                        else             ++zeroCount;
                    }
                    if (sign > 0)  return static_cast<double>(positiveCount);
                    if (sign < 0)  return static_cast<double>(negativeCount);
                    return static_cast<double>(zeroCount);
                }
                if (auto* d = std::get_if<double>(&arg0)) {
                    // 单 double：仅一个元素，按 sign 判断是否匹配
                    double v = *d;
                    bool match = (sign > 0  && v > 0)
                              || (sign < 0  && v < 0)
                              || (sign == 0 && v == 0);
                    return match ? 1.0 : 0.0;
                }
                return 0.0;
            }
            if (funcName == "abs") {
                auto arg = evalNode(symbol, *args[0], context);
                return applyUnaryMath(arg, std::abs);
            }
            if (funcName == "exp") {
                auto arg = evalNode(symbol, *args[0], context);
                return applyUnaryMath(arg, std::exp);
            }
            if (funcName == "log") {
                auto arg = evalNode(symbol, *args[0], context);
                return applyUnaryMath(arg, std::log);
            }
            if (funcName == "sqrt") {
                auto arg = evalNode(symbol, *args[0], context);
                return applyUnaryMath(arg, std::sqrt);
            }
            if (funcName == "sigmoid") {
                auto arg = evalNode(symbol, *args[0], context);
                return applyUnaryMath(arg, sigmoid_fn);
            }
        }
        
        // 二元函数
        if (args.size() == 2) {
            if (funcName == "argmax") {
                // 2 个标量: 返回较大者所在位置 (0 或 1)
                // NaN 语义: 匹配 numpy np.argmax([a, b])（与 N-arg 同算法）
                //   - cur > best_val:                      cur 胜，update
                //   - cur=NaN, best=finiite:                NaN 传染 best，update
                //   - 其他（含 tie / best 已是 NaN 等）:    不动
                // numpy 一旦 NaN 传染，后续 finite 即使更大也无法胜出
                double best_val = ctxToDouble(evalNode(symbol, *args[0], context));
                size_t best_idx = 0;
                double cur = ctxToDouble(evalNode(symbol, *args[1], context));
                if (cur > best_val) {
                    return 1.0;
                } else if (std::isnan(cur) && !std::isnan(best_val)) {
                    return 1.0;
                }
                return static_cast<double>(best_idx);
            }
            if (funcName == "min") {
                auto a = evalNode(symbol, *args[0], context);
                auto b = evalNode(symbol, *args[1], context);
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
            if (funcName == "max") {
                auto a = evalNode(symbol, *args[0], context);
                auto b = evalNode(symbol, *args[1], context);
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

        // N-arg argmax (3+ 标量): argmax(p0, p1, p2)
        // NaN 语义: 与 2-arg 一致，匹配 numpy np.argmax
        //   - cur > best_val:                      cur 胜，update
        //   - cur=NaN, best=finiite:                NaN 传染 best，update
        //   - 其他（含 tie / best 已是 NaN 等）:    不动
        if (args.size() >= 3 && funcName == "argmax") {
            double best_val = ctxToDouble(evalNode(symbol, *args[0], context));
            size_t best_idx = 0;
            for (size_t i = 1; i < args.size(); ++i) {
                double cur = ctxToDouble(evalNode(symbol, *args[i], context));
                if (cur > best_val) {
                    best_val = cur;
                    best_idx = i;
                } else if (std::isnan(cur) && !std::isnan(best_val)) {
                    // NaN 传染: 当前是 NaN，之前是 finite
                    best_val = cur;  // 现在 best_val 是 NaN
                    best_idx = i;
                }
            }
            return static_cast<double>(best_idx);
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
        result = statement::arithmeticMap()[op](result, operand);
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
