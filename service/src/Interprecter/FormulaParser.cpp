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
#include <limits>

#define INTRINSIC_TOPK      "topk"
#define INTRINSIC_BOTTOMK   "bottomk"
#define INTRINSIC_RANK      "rank"
#define INTRINSIC_ZSCORE    "zscore"
#define INTRINSIC_PERCENTILE "pct"
#define INTRINSIC_CS_COUNT  "cs_count"
#define INTRINSIC_CS_SIZE   "cs_size"

// 声明命名空间中的外部符号
extern bool check_bool(const context_t& feature);

namespace {

/**
 * @brief 截面评分的有效值/无效值切分
 *
 * 基本面这类特征在首次公告之前必然是 NaN。NaN 参与 std::sort / partial_sort
 * 会破坏严格弱序（标准未定义行为，排序结果可能整段错乱），所以所有截面
 * 统计都必须先把 NaN 摘出去，只在有效值之间排名，NaN 标的单独输出 NaN。
 */
struct CrossSectionScores {
    Vector<std::pair<symbol_t, double>> valid;  // 有效值，保持 pool 顺序
    Vector<symbol_t> invalid;                    // NaN 标的
};

CrossSectionScores splitValidScores(const Vector<std::pair<symbol_t, double>>& scores) {
    CrossSectionScores out;
    for (const auto& item : scores) {
        if (std::isnan(item.second)) {
            out.invalid.push_back(item.first);
        } else {
            out.valid.push_back(item);
        }
    }
    return out;
}

/**
 * @brief 并列名次取平均（1-based），与 pandas rank(method="average") 一致
 *
 * 输入必须已按值升序排序。返回与输入等长的名次数组。
 */
Vector<double> averageRanks(const Vector<std::pair<symbol_t, double>>& sorted) {
    Vector<double> ranks(sorted.size(), 0.0);
    size_t i = 0;
    while (i < sorted.size()) {
        size_t j = i;
        while (j + 1 < sorted.size() && sorted[j + 1].second == sorted[i].second) {
            ++j;
        }
        // 并列段 [i, j] 取平均名次，名次从 1 开始
        double avg = (static_cast<double>(i) + static_cast<double>(j)) / 2.0 + 1.0;
        for (size_t k = i; k <= j; ++k) {
            ranks[k] = avg;
        }
        i = j + 1;
    }
    return ranks;
}

}  // namespace

// ========== FormulaParser 核心方法实现 ==========

String FormulaParser::cleanInputString(const String& input) {
    String result;
    for (char c : input) {
        if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r') {
            result += c;
        }
    }
    return result;
}

FormulaParser::FormulaParser(Server* server): _server(server), _default(TradeAction::HOLD) {
    _parser.set_logger([](size_t line, size_t col, const std::string& msg) {
        auto info = fmt::format("{} {}: {}", line, col, msg);
        strategy_error("", info);
        INFO("parse fail: {}", info);
    });
    _parser.enable_packrat_parsing();
    if (!_parser.load_grammar(statement::grammar)) {
        return ;
    }
    _parser.enable_ast();
}

bool FormulaParser::parse(const String& code) {
    _codes = cleanInputString(code);
    DEBUG_INFO("Parsing: '{}'", _codes);
    if (_parser.parse(_codes, _ast)) {
        // printAST(_ast);
        _ast = _parser.optimize_ast(_ast);

#ifdef _DEBUG
        INFO("** Node: {}, token: {}", _ast->name, _ast->token);
        printAST(_ast);
#endif
        _init = true;
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
        INFO("{}Node: {}, token: {}", tabs, node->name, node->token);
        printAST(node, ++lvl);
    }
}

bool FormulaParser::parse(const String& code, TradeAction action) {
    _default = action;
    return parse(code);
}

CrossSectionFuncType FormulaParser::getFuncType(const String& name) {
    static const Map<String, CrossSectionFuncType> typeMap{
        {INTRINSIC_TOPK, CrossSectionFuncType::TOPK},
        {INTRINSIC_BOTTOMK, CrossSectionFuncType::BOTTOMK},
        {INTRINSIC_RANK, CrossSectionFuncType::RANK},
        {INTRINSIC_ZSCORE, CrossSectionFuncType::ZSCORE},
        {INTRINSIC_PERCENTILE, CrossSectionFuncType::PERCENTILE},
        {INTRINSIC_CS_COUNT, CrossSectionFuncType::CS_COUNT},
        {INTRINSIC_CS_SIZE, CrossSectionFuncType::CS_SIZE}
    };

    auto it = typeMap.find(name);
    return (it != typeMap.end()) ? it->second : CrossSectionFuncType::RAW;
}

bool FormulaParser::isCrossSectionFunction(const String& funName) {
    static const UnorderedSet<String> crossFuncs{
        INTRINSIC_TOPK,
        INTRINSIC_BOTTOMK,
        INTRINSIC_RANK,
        INTRINSIC_ZSCORE,
        INTRINSIC_PERCENTILE,
        INTRINSIC_CS_COUNT,
        INTRINSIC_CS_SIZE,
    };
    return crossFuncs.count(funName);
}

bool FormulaParser::hasCrossSectionFunctions(const peg::Ast& ast) {
    std::stack<const peg::Ast*> stack;
    stack.push(&ast);

    while (!stack.empty()) {
        const peg::Ast* current = stack.top();
        stack.pop();

        if (current->name == "FunctionCall") {
            if (!current->nodes.empty()) {
                auto& firstChild = current->nodes[0];
                if (firstChild->name == "Identifier") {
                    String funcName(firstChild->token);
                    if (isCrossSectionFunction(funcName)) {
                        return true;
                    }
                }
            }
        }

        for (auto it = current->nodes.rbegin(); it != current->nodes.rend(); ++it) {
            stack.push(it->get());
        }
    }
    return false;
}

// 从 AST 节点中递归提取 Number 值
static std::optional<double> tryExtractNumber(const std::shared_ptr<peg::Ast>& node) {
    if (node->name == "Number") {
        return node->token_to_number<double>();
    }
    // 处理一元负号: Unary <- UnaryOp(-) Primary(Number)
    if (node->name == "Unary" && node->nodes.size() == 2) {
        auto& op = node->nodes[0];
        if (op->name == "UnaryOp" && op->token == "-") {
            auto inner = tryExtractNumber(node->nodes[1]);
            if (inner) return -(*inner);
        }
    }
    // Primary / Atom 等透传节点，递归到子节点
    if (node->nodes.size() == 1) {
        return tryExtractNumber(node->nodes.front());
    }
    return std::nullopt;
}

// 从表达式提取截面函数并建图（递归）
std::string FormulaParser::extractAndBuildGraph(const peg::Ast& node, int& counter) {
    if (node.name == "FunctionCall") {
        String funcName(node.nodes[0]->token);
        if (!isCrossSectionFunction(funcName)) {
            // 不是截面函数，递归处理子节点
            for (auto& child : node.nodes) {
                extractAndBuildGraph(*child, counter);
            }
            return "";
        }

        // 创建截面节点
        String nodeId = "__cs_" + std::to_string(counter++) + "__";
        auto& csNode = _csGraph.addNode(nodeId);
        csNode.name = funcName;
        csNode.type = getFuncType(funcName);

        // 解析参数
        if (node.nodes.size() > 1) {
            auto& argsNode = node.nodes[1];  // Arguments
            if (argsNode->name == "Arguments" && !argsNode->nodes.empty()) {
                // 第一个参数是表达式
                auto& firstArg = argsNode->nodes[0];
                String innerDep = extractAndBuildGraph(*firstArg, counter);
                if (!innerDep.empty()) {
                    csNode.dependencies.push_back(innerDep);
                    _csGraph.addEdge(nodeId, innerDep);
                }
                csNode.exprAst = firstArg;

                // 第二个参数（如果有，如 topk 的 k 值）
                if (argsNode->nodes.size() > 1) {
                    auto& secondArg = argsNode->nodes[1];
                    if (auto num = tryExtractNumber(secondArg)) {
                        csNode.param = *num;
                    }
                }
            }
        }

        // 注册 AST 节点地址到节点 ID 的映射（每个 FunctionCall 地址唯一）
        _csAstNodeToId[&node] = nodeId;
        return nodeId;
    }
    else {
        // 非函数调用，递归处理子节点
        for (auto& child : node.nodes) {
            extractAndBuildGraph(*child, counter);
        }
        return "";
    }
}

// 从 AST 构建图
void FormulaParser::buildCrossSectionGraph(const peg::Ast& ast) {
    _csGraph.clear();
    _csAstNodeToId.clear();

    int nodeCounter = 0;
    extractAndBuildGraph(ast, nodeCounter);

    // 拓扑排序
    if (!_csGraph.topologicalSort()) {
        WARN("CrossSectionGraph has cycle!");
    }
}

// 计算单个节点
void FormulaParser::computeNode(CrossSectionNode& node, const Vector<symbol_t>& symbols, DataContext& context) {
    if (node.computed) return;

    // 先计算所有依赖
    for (auto& depId : node.dependencies) {
        auto it = _csGraph.nodes.find(depId);
        if (it != _csGraph.nodes.end()) {
            computeNode(it->second, symbols, context);
        }
    }

    // 收集依赖节点的结果（用于嵌套函数）
    Map<symbol_t, double> depValues;
    for (auto& depId : node.dependencies) {
        auto it = _csGraph.nodes.find(depId);
        if (it != _csGraph.nodes.end() && it->second.computed) {
            for (auto& [sym, val] : it->second.outputs) {
                // 支持 double 和 bool 类型的依赖值
                if (std::holds_alternative<double>(val)) {
                    depValues[sym] = std::get<double>(val);
                } else if (std::holds_alternative<bool>(val)) {
                    // 将 bool 转换为 double (true -> 1.0, false -> 0.0)
                    depValues[sym] = std::get<bool>(val) ? 1.0 : 0.0;
                }
            }
        }
    }

    // 为每个 symbol 计算表达式值
    Vector<std::pair<symbol_t, double>> scores;
    for (auto symbol : symbols) {
        double score = 0.0;

        switch (node.type) {
        case CrossSectionFuncType::RAW:
        case CrossSectionFuncType::TOPK:
        case CrossSectionFuncType::BOTTOMK:
        case CrossSectionFuncType::CS_COUNT:
            // 计算表达式值
            {
                context_t val = evalNode(symbol, *node.exprAst, context);
                if (std::holds_alternative<double>(val)) {
                    score = std::get<double>(val);
                } else if (std::holds_alternative<Vector<double>>(val)) {
                    // evalIdentifier 从 context 返回时间序列时，取最新值作为截面评分
                    auto& vec = std::get<Vector<double>>(val);
                    // 空序列 = 该标的无任何数据，属于未知而非 0
                    score = vec.empty() ? std::numeric_limits<double>::quiet_NaN() : vec.back();
                } else if (std::holds_alternative<bool>(val)) {
                    score = std::get<bool>(val) ? 1.0 : 0.0;
                }
            }
            break;

        case CrossSectionFuncType::RANK:
        case CrossSectionFuncType::ZSCORE:
        case CrossSectionFuncType::PERCENTILE:
            // 嵌套调用（pct(rank(close))）用内层节点的结果；
            // 单参数调用（pct(close)）没有内层节点，直接对表达式求值。
            // 两者都取不到时视为未知（NaN），不能当成 0 参与排名。
            if (depValues.count(symbol)) {
                score = depValues[symbol];
            } else if (node.exprAst) {
                context_t val = evalNode(symbol, *node.exprAst, context);
                if (std::holds_alternative<double>(val)) {
                    score = std::get<double>(val);
                } else if (std::holds_alternative<Vector<double>>(val)) {
                    // evalIdentifier 从 context 返回时间序列时，取最新值作为截面评分
                    auto& vec = std::get<Vector<double>>(val);
                    // 空序列 = 该标的无任何数据，属于未知而非 0
                    score = vec.empty() ? std::numeric_limits<double>::quiet_NaN() : vec.back();
                } else if (std::holds_alternative<bool>(val)) {
                    score = std::get<bool>(val) ? 1.0 : 0.0;
                }
            } else {
                score = std::numeric_limits<double>::quiet_NaN();
            }
            break;
        }

        scores.emplace_back(symbol, score);
    }

    // 根据函数类型计算输出
    switch (node.type) {
    case CrossSectionFuncType::TOPK: {
        int k = 10; // 默认值
        if (std::holds_alternative<double>(node.param)) {
            k = static_cast<int>(std::get<double>(node.param));
        }
        // NaN 不参与选取：基本面这类特征缺失时不该被选进 topk
        auto split = splitValidScores(scores);
        int n = static_cast<int>(split.valid.size());

        Set<symbol_t> topKSymbols;
        if (n > 0) {
            k = std::max(1, std::min(k, n));
            // 部分排序选前 k
            std::partial_sort(split.valid.begin(), split.valid.begin() + k, split.valid.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            for (int i = 0; i < k; ++i) {
                topKSymbols.insert(split.valid[i].first);
            }
        }

        for (auto symbol : symbols) {
            node.outputs[symbol] = (topKSymbols.count(symbol) > 0);
        }
        break;
    }

    case CrossSectionFuncType::BOTTOMK: {
        int k = 10; // 默认值
        if (std::holds_alternative<double>(node.param)) {
            k = static_cast<int>(std::get<double>(node.param));
        }
        auto split = splitValidScores(scores);
        int n = static_cast<int>(split.valid.size());

        Set<symbol_t> bottomKSymbols;
        if (n > 0) {
            k = std::max(1, std::min(k, n));
            // 部分排序选后 k（从小到大排序）
            std::partial_sort(split.valid.begin(), split.valid.begin() + k, split.valid.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            for (int i = 0; i < k; ++i) {
                bottomKSymbols.insert(split.valid[i].first);
            }
        }

        for (auto symbol : symbols) {
            node.outputs[symbol] = (bottomKSymbols.count(symbol) > 0);
        }
        break;
    }

    case CrossSectionFuncType::RANK: {
        // 升序名次归一化到 0~1：值最高的得 1.0，最低的得 0.0
        // 并列取平均名次；NaN 不参与排名，输出 NaN
        auto split = splitValidScores(scores);
        auto sorted = split.valid;
        std::stable_sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

        int n = static_cast<int>(sorted.size());
        auto ranks = averageRanks(sorted);
        for (int i = 0; i < n; ++i) {
            double rank = (n > 1) ? ((ranks[i] - 1.0) / (n - 1)) : 0.5;
            node.outputs[sorted[i].first] = rank;
        }
        for (auto sym : split.invalid) {
            node.outputs[sym] = std::numeric_limits<double>::quiet_NaN();
        }
        break;
    }

    case CrossSectionFuncType::ZSCORE: {
        // 均值/标准差只在有效值上计算；NaN 标的输出 NaN
        auto split = splitValidScores(scores);
        if (split.valid.empty()) {
            for (auto sym : split.invalid) {
                node.outputs[sym] = std::numeric_limits<double>::quiet_NaN();
            }
            break;
        }

        double sum = 0.0;
        for (auto& item : split.valid) {
            sum += item.second;
        }
        double mean = sum / split.valid.size();

        double sq_sum = 0.0;
        for (auto& item : split.valid) {
            sq_sum += (item.second - mean) * (item.second - mean);
        }
        double std = std::sqrt(sq_sum / split.valid.size());

        // 标准化
        for (auto& item : split.valid) {
            node.outputs[item.first] = (std > 1e-10) ? ((item.second - mean) / std) : 0.0;
        }
        for (auto sym : split.invalid) {
            node.outputs[sym] = std::numeric_limits<double>::quiet_NaN();
        }
        break;
    }

    case CrossSectionFuncType::PERCENTILE: {
        auto split = splitValidScores(scores);
        auto sorted = split.valid;
        std::stable_sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

        int n = static_cast<int>(sorted.size());

        // 带第二参数 → 截面 p 分位数（所有标的一个标量值）
        // node.param 默认构造是 bool(false)，只有解析到数字时才是 double，
        // 因此 holds_alternative<double> 正好能区分"没传参"和"传了 0"。
        if (std::holds_alternative<double>(node.param)) {
            double p = std::get<double>(node.param);
            p = std::max(0.0, std::min(1.0, p));
            double q = std::numeric_limits<double>::quiet_NaN();
            if (n > 0) {
                // 线性插值，与 numpy.quantile(method="linear") / pandas.quantile 一致
                double pos = p * (n - 1);
                size_t lo = static_cast<size_t>(std::floor(pos));
                size_t hi = static_cast<size_t>(std::ceil(pos));
                double frac = pos - static_cast<double>(lo);
                q = sorted[lo].second + frac * (sorted[hi].second - sorted[lo].second);
            }
            for (auto symbol : symbols) {
                node.outputs[symbol] = q;
            }
            break;
        }

        // 单参数 → 截面百分位排名：升序名次 / 有效标的数
        // 值最高的得 1.0，最低的得 1/n；与 pandas rank(pct=True) 一致
        // （含并列取平均名次）
        auto ranks = averageRanks(sorted);
        for (int i = 0; i < n; ++i) {
            node.outputs[sorted[i].first] = (n > 0) ? (ranks[i] / n) : 0.0;
        }
        for (auto sym : split.invalid) {
            node.outputs[sym] = std::numeric_limits<double>::quiet_NaN();
        }
        break;
    }

    case CrossSectionFuncType::CS_COUNT: {
        // cs_count(expr, sign): 统计当前 bar 跨标的满足条件的数量
        //   sign > 0:  score > 0 的标的数
        //   sign < 0:  score < 0 的标的数
        //   sign == 0: score == 0 的标的数
        double sign = 1.0;
        if (std::holds_alternative<double>(node.param)) {
            sign = std::get<double>(node.param);
        }
        double cnt = 0.0;
        for (auto& [sym, score] : scores) {
            if (sign > 0 && score > 0)       ++cnt;
            else if (sign < 0 && score < 0)  ++cnt;
            else if (sign == 0 && score == 0) ++cnt;
        }
        // 所有标的输出相同的计数值
        for (auto& [sym, score] : scores) {
            node.outputs[sym] = cnt;
        }
        break;
    }

    case CrossSectionFuncType::CS_SIZE: {
        // cs_size(): 返回标的总数（所有标的输出相同值）
        double sz = static_cast<double>(symbols.size());
        for (auto& [sym, score] : scores) {
            node.outputs[sym] = sz;
        }
        break;
    }

    default:
    case CrossSectionFuncType::RAW: {
        // RAW：直接输出分数
        for (auto& [sym, score] : scores) {
            node.outputs[sym] = score;
        }
        break;
    }
    }

    node.computed = true;
}

// 执行整个图
void FormulaParser::computeCrossSectionGraph(const Vector<symbol_t>& symbols, DataContext& context) {
    // 按拓扑序计算所有节点
    for (auto& nodeId : _csGraph.evalOrder) {
        auto& node = _csGraph.nodes.at(nodeId);
        computeNode(node, symbols, context);
    }
}

List<Pair<symbol_t, TradeAction>> FormulaParser::envoke(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context) {
    List<Pair<symbol_t, TradeAction>> decisions;
    if (hasCrossSectionFunctions(*_ast)) {
        return envokeMixedCase(symbols, variantNames, context);
    } else {
        for (auto symbol: symbols) {
            auto exprValue = eval(symbol, *_ast, context);
            bool boolValue = std::get<bool>(exprValue);
            TradeAction action = (boolValue ? _default : TradeAction::HOLD);

            Pair<symbol_t, TradeAction> decision{symbol, action};
            decisions.emplace_back(std::move(decision));
        }
    }
    return decisions;
}

List<Pair<symbol_t, TradeAction>> FormulaParser::envokeMixedCase(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context) {
    List<Pair<symbol_t, TradeAction>> decisions;

    // Step 1: 从 AST 构建图
    buildCrossSectionGraph(*_ast);

    // Step 2: 执行图计算
    computeCrossSectionGraph(symbols, context);

    // Step 3: 为每个 symbol 求值
    for (auto symbol : symbols) {
        context_t exprValue = evalNode(symbol, *_ast, context);
        bool boolValue = statement::check_bool(exprValue);
        TradeAction action = (boolValue ? _default : TradeAction::HOLD);
        decisions.emplace_back(symbol, action);
    }

    return decisions;
}
