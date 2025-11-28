#pragma once
#include "std_header.h"
#include "peglib.h"
#include "Util/system.h"

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

class DataContext;
// 交易操作类型
enum class TradeAction {
    HOLD,
    BUY,
    SELL,
    EXEC,
};
// 交易决策结果
struct TradeDecision {
    TradeAction action;
    symbol_t symbol;
    int quantity;           // 数量
    double price;           // 建议价格
    String reason;     // 决策原因
};

struct symbol_t;
// 解析器定义
class FormulaParser {
    using intrinsic_function = std::function<feature_t(const std::vector<feature_t>&)>;
public:
    FormulaParser(Server* server);

    bool parse(const String& code);
    bool parse(const String& code, TradeAction action);

    List<TradeDecision> envoke(const Vector<symbol_t>& symbols, const Set<String>& variantNames, DataContext& context);

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
    feature_t getVariableValue(const symbol_t& symbol, const String& varName, DataContext* context);

    // 根据表达式值生成交易决策
    TradeDecision makeDecision(const symbol_t& symbol, bool exprValue, DataContext& context);

    
private:
    peg::parser _parser;
    String _codes;
    std::shared_ptr<peg::Ast> _ast;
    Server* _server;
    TradeAction _default;

    std::unordered_map<String, intrinsic_function> _functions;
};