#pragma once
#include "std_header.h"
#include "peglib.h"
#include "Util/system.h"
#include "DataContext.h"

class Server;
// 语句执行结果
struct StatementResult {
    feature_t _value;
    bool _has_return;
    
    StatementResult() : _has_return(false) {}
    StatementResult(const feature_t& f) : _value(f), _has_return(true) {}
};

namespace peg{
    class parser;
}

struct CrossSectionResult {
    String funcName;
    std::shared_ptr<peg::Ast> funcAst;
    Map<symbol_t, bool> stockResults; // 每个股票的结果
};

struct cross_function_t {
    String _name;
    Map<char, context_t> _args;  // 参数占位符
    // 返回类型
};

struct symbol_t;
// 解析器定义
class FormulaParser {
    using intrinsic_function = std::function<feature_t(const std::vector<feature_t>&)>;
public:
    FormulaParser(Server* server);

    bool parse(const String& code);
    bool parse(const String& code, TradeAction action);

    List<Pair<symbol_t, TradeAction>> envoke(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context);

public:
    feature_t evalNumber(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    feature_t evalIdentifier(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    feature_t evalComparison(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    // 处理函数调用的辅助函数
    feature_t evalFunctionCall(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    feature_t evalTerm(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    feature_t evalProgram(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    feature_t evalOrExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    feature_t evalAndExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    feature_t evalNotExpr(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);

    feature_t evalStatement(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    feature_t evalPrimary(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    feature_t evalArithmetic(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);
    feature_t evalNode(const symbol_t& symbol, const peg::Ast&, DataContext& context);
private:
    String cleanInputString(const String& input);

    void registerFunction(const std::string& name, intrinsic_function func) {
        _functions[name] = func;
    }

    feature_t eval(const symbol_t& symbol, const peg::Ast& ast, DataContext& context);

    feature_t evalTrailer(const symbol_t& symbol, const feature_t& base, const peg::Ast& ast, DataContext& context);

    feature_t evalTimeIndex(const symbol_t& symbol, const feature_t& base, const peg::Ast& ast, DataContext& context);
    
    double getHistoricalValue(const symbol_t& symbol, const feature_t& base, int time_offset, DataContext& context);
    // 获取变量值的辅助函数
    context_t getVariableValue(const symbol_t& symbol, const String& varName, DataContext* context);

    void printAST(std::shared_ptr<peg::Ast> ast, int lvl = 0);

    bool hasCrossSectionFunctions(const peg::Ast& ast);

    bool isCrossSectionFunction(const String& funName);
    // 处理截面函数混合的情况
    List<Pair<symbol_t, TradeAction>> envokeMixedCase(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context);
    // 提取表达式中的所有截面函数
    void extractCrossSectionFunctions(const peg::Ast& ast);
    // 计算截面函数，结果保存到context中
    void precomputeCrossSectionFunctions(const Vector<symbol_t>& symbols, DataContext& context);

    feature_t evaluateForSymbolWithCrossSectionResults(const symbol_t& symbol, const peg::Ast& ast, DataContext& context,
        const Map<String, std::shared_ptr<CrossSectionResult>>& crossSectionResults);

private:
    String genPrimaryPlaceHolder(const peg::Ast& ats);

private:
    void topk(const Vector<symbol_t>& allSymbols, const peg::Ast& funcAst, CrossSectionResult& result, DataContext& context);

private:
    peg::parser _parser;
    String _codes;
    std::shared_ptr<peg::Ast> _ast;
    Server* _server;
    TradeAction _default;
    // 变量名-函数信息
    Map<String, cross_function_t> _CSFunctions;

    std::unordered_map<String, intrinsic_function> _functions;
};