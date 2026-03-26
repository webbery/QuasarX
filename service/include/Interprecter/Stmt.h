#pragma once
#include "std_header.h"
#include "peglib.h"
#include "Util/system.h"
#include "DataContext.h"

class Server;

// 语句执行结果
struct StatementResult {
    context_t _value;
    bool _has_return;

    StatementResult() : _has_return(false) {}
    StatementResult(const context_t& f) : _value(f), _has_return(true) {}
};

namespace peg{
    class parser;
}

// 截面函数结果（保留向后兼容）
struct CrossSectionResult {
    String funcName;
    std::shared_ptr<peg::Ast> funcAst;
    Map<symbol_t, bool> stockResults;
};

// 截面函数类型
enum class CrossSectionFuncType {
    TOPK,           // topk(expr, k) - 选前 k 名
    BOTTOMK,        // bottomk(expr, k) - 选后 k 名
    RANK,           // rank(expr) - 返回排名 (0~1)
    ZSCORE,         // zscore(expr) - 标准化
    PERCENTILE,     // percentile(expr, p) - 分位数
    RAW             // 原始分数透传
};

// 截面函数节点（DAG 中的节点）
struct CrossSectionNode {
    String id;                        // 唯一标识，如 "__cs_0__"
    String name;                      // 函数名，如 "topk"
    CrossSectionFuncType type;        // 函数类型
    std::shared_ptr<peg::Ast> exprAst; // 内部表达式 AST
    Vector<String> dependencies;      // 依赖的节点 ID（用于嵌套）
    context_t param;                  // 额外参数（如 topk 的 k 值）

    // 计算结果：key 是 symbol，value 是计算结果（double 或 bool）
    Map<symbol_t, context_t> outputs;

    // 状态标记
    bool computed = false;
    int inDegree = 0;  // 入度，用于拓扑排序

    CrossSectionNode() : type(CrossSectionFuncType::RAW) {}
};

// 截面函数图（管理所有节点和依赖关系）
struct CrossSectionGraph {
    Map<String, CrossSectionNode> nodes;  // 所有节点
    Vector<String> evalOrder;             // 拓扑排序后的求值顺序

    // 添加节点
    CrossSectionNode& addNode(const String& id);

    // 添加依赖：from 依赖 to
    void addEdge(const String& from, const String& to);

    // 拓扑排序，返回是否成功（无环）
    bool topologicalSort();

    // 清空图
    void clear();

    // 判断是否为空
    bool empty() const { return nodes.empty(); }
};

struct cross_function_t {
    String _name;
    Map<char, context_t> _args;
};

struct symbol_t;

// 解析器定义
class FormulaParser {
    using intrinsic_function = std::function<context_t(const std::vector<context_t>&)>;
public:
    FormulaParser(Server* server);

    bool parse(const String& code);
    bool parse(const String& code, TradeAction action);

    List<Pair<symbol_t, TradeAction>> envoke(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context);

public:
    context_t evalNumber(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalIdentifier(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalComparison(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalFunctionCall(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalTerm(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalProgram(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalOrExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalAndExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalNotExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalStatement(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalPrimary(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalArithmetic(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    context_t evalNode(const symbol_t& symbol, const peg::Ast&, DataContext& context);

private:
    String cleanInputString(const String& input);

    void registerFunction(const std::string& name, intrinsic_function func) {
        _functions[name] = func;
    }

    context_t eval(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);

    context_t evalTrailer(const symbol_t& symbol, const context_t& base, const peg::Ast& ast, DataContext& context);

    context_t evalTimeIndex(const symbol_t& symbol, const context_t& base, const peg::Ast& ast, DataContext& context);

    double getHistoricalValue(const symbol_t& symbol, const context_t& base, int time_offset, DataContext& context);

    context_t getVariableValue(const symbol_t& symbol, const String& varName, DataContext* context);

    void printAST(std::shared_ptr<peg::Ast> ast, int lvl = 0);

    bool hasCrossSectionFunctions(const peg::Ast& ast);

    bool isCrossSectionFunction(const String& funName);

    CrossSectionFuncType getFuncType(const String& name);

    // 处理截面函数混合的情况
    List<Pair<symbol_t, TradeAction>> envokeMixedCase(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context);

    // 提取表达式中的所有截面函数
    void extractCrossSectionFunctions(const peg::Ast& ast);

    // 计算截面函数，结果保存到 context 中
    void precomputeCrossSectionFunctions(const Vector<symbol_t>& symbols, DataContext& context);

    context_t evaluateForSymbolWithCrossSectionResults(const symbol_t& symbol, const peg::Ast& ast, DataContext& context,
        const Map<String, std::shared_ptr<CrossSectionResult>>& crossSectionResults);

private:
    String genPrimaryPlaceHolder(const peg::Ast& ats);

private:
    void topk(const Vector<symbol_t>& allSymbols, const peg::Ast& funcAst, CrossSectionResult& result, DataContext& context);

    // ========== 新增：基于图的截面函数处理 ==========

    // 从 AST 构建图
    void buildCrossSectionGraph(const peg::Ast& ast);

    // 执行图计算
    void computeCrossSectionGraph(const Vector<symbol_t>& symbols, DataContext& context);

    // 计算单个节点
    void computeNode(CrossSectionNode& node, const Vector<symbol_t>& symbols, DataContext& context);

    // 从表达式提取截面函数并建图（递归）
    std::string extractAndBuildGraph(const peg::Ast& node, int& counter);

private:
    peg::parser _parser;
    String _codes;
    std::shared_ptr<peg::Ast> _ast;
    Server* _server;
    TradeAction _default;

    // 变量名 -> 截面节点 ID 映射（用于查找）
    Map<String, String> _varToNodeId;

    // 截面函数图
    CrossSectionGraph _csGraph;

    // 原有结构（保留兼容）
    Map<String, cross_function_t> _CSFunctions;

    std::unordered_map<String, intrinsic_function> _functions;
};
