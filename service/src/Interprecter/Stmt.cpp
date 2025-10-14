#include "Interprecter/Stmt.h"
#include "Util/string_algorithm.h"
#include "server.h"

#define ANY_CAST(val) any_cast<std::shared_ptr<Stmt>>(val)

namespace  {
String grammar = R"(
    # 顶层的公式规则，可以是赋值语句或一个表达式
    Formula        <- Assignment / Expression

    # 赋值语句：变量名 '=' 表达式
    Assignment     <- Identifier WS* '=' WS* Expression
    # 表达式：目前主要是函数调用、变量或数字，可扩展支持更多运算符
    Expression     <- FunctionCall / Primary
    # 函数调用：函数名 '(' 参数列表 ')'
    FunctionCall   <- Identifier '(' WS* Args? WS* ')'
    # 参数列表：参数之间用逗号分隔
    Args           <- Expression (WS* ',' WS* Expression)*
    # 基础元素：标识符、数字或括号包裹的表达式
    Primary        <- Identifier / Number / '(' WS* Expression WS* ')'
    # 标识符：由字母或下划线开头，后接字母、数字或下划线
    Identifier     <- < [a-zA-Z_] [a-zA-Z_0-9]* >
    # 数字：支持整数和小数
    Number         <- < [0-9]+ ('.' [0-9]*)? >
    # 空白字符：包括空格、制表符等
    WS             <- [ \t\r\n]
)";
}

double FunctionCallStmt::evaluate(Server* server) const {
    return 0;
}

FormulaParser::FormulaParser(Server* server): _server(server) {
    if (!_parser.load_grammar(grammar)) {
        return ;
    }
    // 设置语法规则对应的语义动作（用于构建AST）
    _parser["Formula"] = [](const peg::SemanticValues& sv) -> std::shared_ptr<Stmt> {
        switch (sv.choice()) {
            case 0: return any_cast<std::shared_ptr<Stmt>>(sv[0]); // 返回 Assignment 的节点
            default: return ANY_CAST(sv[0]); // 返回 Expression 的节点
        }
    };

    _parser["Assignment"] = [](const peg::SemanticValues& sv) -> std::shared_ptr<Stmt> {
        String var_name = any_cast<String>(sv[0]);
        std::shared_ptr<Stmt> expr = ANY_CAST(sv[1]);
        // 创建一个 AssignmentNode，将变量名和表达式节点关联起来
        return make_shared<AssignmentStmt>(var_name, std::move(expr));
    };

    _parser["Expression"] = [](const peg::SemanticValues& sv) {
        return any_cast<std::shared_ptr<Stmt>>(sv[0]); // 返回 FunctionCall 或 Primary 的节点
    };

    _parser["FunctionCall"] = [](const peg::SemanticValues& sv) {
        String func_name = std::any_cast<String>(sv[0]);
        auto func_node = make_shared<FunctionCallStmt>(func_name);
        if (sv.size() > 1) {
            // 如果有参数，获取参数列表（Args 规则产生的 vector）
            auto& args = std::any_cast<const Vector<std::shared_ptr<Stmt>>&>(sv[1]);
            for (auto& arg : args) {
                func_node->arguments.emplace_back(std::move(arg));
            }
        }
        return func_node;
    };

    _parser["Args"] = [](const peg::SemanticValues& sv) -> Vector<std::shared_ptr<Stmt>> {
        Vector<std::shared_ptr<Stmt>> args;
        for (auto& arg : sv) {
            auto& non_const_args = const_cast<std::any&>(arg);
            args.emplace_back(std::move(any_cast<std::shared_ptr<Stmt>&>(non_const_args)));
        }
        return args;
    };

    _parser["Primary"] = [](const peg::SemanticValues& sv) {
        return  ANY_CAST(sv[0]); // 返回 Identifier, Number 或括号内 Expression 的节点
    };

    _parser["Identifier"] = [](const peg::SemanticValues& sv) {
        auto str = sv.token();
        String name(str.data(), str.size());
        // 注意：这里我们直接返回一个 VariableNode。
        // 在更复杂的解析器中，可能需要区分它是赋值语句左边的变量（目标）还是表达式中的变量引用。
        return std::make_shared<VariableStmt>(name);
    };

    _parser["Number"] = [](const peg::SemanticValues& sv) -> std::shared_ptr<NumberStmt> {
        double value = std::stof(sv.token().data());
        return std::make_shared<NumberStmt>(value);
    };
}


bool FormulaParser::parse(const String& code) {
    List<String> lines;
    split(code, lines, "\n");
    for (const auto& formula : lines) {
        std::shared_ptr<Stmt> ast;
        if (_parser.parse(formula, ast)) {
            try {
                double result = ast->evaluate(_server);
                // 检查结果是否是赋值语句，并做相应输出
                if (auto assignment = dynamic_cast<AssignmentStmt*>(ast.get())) {
                }
            } catch (const std::exception& e) {
                FATAL("Evaluation error: {}", e.what());
                return false;
            }
        } else {
            FATAL("Parse failed for formula: {}", formula);
            return false;
        }
    }
    return true;
}