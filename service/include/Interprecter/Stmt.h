#pragma once
#include "std_header.h"
#include "peglib.h"
#include "Util/system.h"

class Server;
struct Stmt {
    virtual ~Stmt() = default;
    virtual double evaluate(Server* server) const = 0;
};

struct NumberStmt : Stmt {
    double value;
    NumberStmt(double v) : value(v) {}
    double evaluate(Server* server) const override {
        return value;
    }
};

struct VariableStmt : Stmt {
    std::string name;
    VariableStmt(const std::string& n) : name(n) {}
    double evaluate(Server* server) const override {
        return 0;
    }
};

struct FunctionCallStmt : Stmt {
    std::string function_name;
    std::vector<std::shared_ptr<Stmt>> arguments;
    
    FunctionCallStmt(const std::string& name) : function_name(name) {}
    
    double evaluate(Server* server) const override;
};

struct AssignmentStmt : Stmt {
    std::string variable_name;
    std::shared_ptr<Stmt> expression;
    
    AssignmentStmt(const std::string& name, std::shared_ptr<Stmt> expr) 
        : variable_name(name), expression(std::move(expr)) {}
    
    double evaluate(Server* server) const override {
        return 0;
    }
    
    const std::string& getVariableName() const { return variable_name; }
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
public:
    FormulaParser(Server* server);

    bool parse(const String& code);
    bool parse(const String& code, TradeAction action);

    List<TradeDecision> envoke(const Vector<symbol_t>& symbols, const List<String>& variantNames, DataContext* context);

private:
    double eval(const symbol_t& symbol, const peg::Ast& ast, DataContext* context);

    double evalNode(const symbol_t& symbol, const peg::Ast&, DataContext* context);
    // 处理函数调用的辅助函数
    double evalFunctionCall(const symbol_t& symbol, const peg::Ast& ast, DataContext* context);
    
    // 获取变量值的辅助函数
    feature_t getVariableValue(const symbol_t& symbol, const String& varName, DataContext* context);

    // 根据表达式值生成交易决策
    TradeDecision makeDecision(const symbol_t& symbol, double exprValue, DataContext* context);
private:
    peg::parser _parser;
    std::shared_ptr<peg::Ast> _ast;
    Server* _server;
    TradeAction _default;
};