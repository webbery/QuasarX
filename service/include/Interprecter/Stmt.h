#pragma once
#include "std_header.h"
#include "peglib.h"

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

// 解析器定义
class FormulaParser {
public:
    FormulaParser(Server* server);

    bool parse(const String& code);

private:
    peg::parser _parser;
    Server* _server;
};