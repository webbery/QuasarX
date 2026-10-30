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

#define ANY_CAST(val) any_cast<std::shared_ptr<Stmt>>(val)

#define INTRINSIC_TOPK      "topk"
#define INTRINSIC_BOTTOMK   "bottomk"
#define INTRINSIC_RANK      "rank"
#define INTRINSIC_ZSCORE    "zscore"
#define INTRINSIC_PERCENTILE "pct"

namespace statement{
String grammar = R"(
        # 程序结构
        Program         <- Statement*
        Statement       <- ExpressionStmt / AssignmentStmt
        ExpressionStmt  <- Expression EOL
        AssignmentStmt  <- Identifier '=' Expression EOL

        # 表达式定义
        Expression      <- OrExpr
        OrExpr          <- AndExpr ('or' AndExpr)*
        AndExpr         <- NotExpr ('and' NotExpr)*
        NotExpr         <- NotPrefix / CompareExpr
        NotPrefix       <- ('not' / '!') NotExpr { no_ast_opt }
        CompareExpr     <- ArithExpr (CompareOp ArithExpr)*
        ArithExpr       <- Term (AddOp Term)*
        Term            <- Unary (MulOp Unary)*
        Unary           <- UnaryOp Unary / Primary { no_ast_opt }
        UnaryOp         <- '-'
        Primary         <- Atom (Trailer)*
        Atom            <- Number / String / BoolLiteral / FunctionCall / ListExpr / Identifier / '(' Expression ')'

        # 下标访问。两种下标语义完全不同，必须在语法上分成两个规则：
        #   [t] / [t-N]  时间序列偏移 —— 读 {symbol}.{var} 的历史某根 bar
        #   [0] / [1]     多列输出的列索引 —— 读 {symbol}.{var}_{N}
        # 合成一条规则的话，解析器看到 [0] 无法判断是「往回 0 根」还是「第 0 列」，
        # 只能运行期探测 key 是否存在才能区分，验证侧就再也对不上求值侧的语义了。
        #
        # 注意：原来的 `'.' Identifier` 已删除。带点变量名（如 profit.roe_avg）
        # 现在由 Identifier 规则直接支持（见下方），不再作为 Trailer 处理。
        Trailer         <- '(' Arguments? ')' / TimeIndex / ColumnIndex
        TimeIndex       <- '[' < 't' '-' [0-9]+ > ']' / '[' < 't' > ']'
        ColumnIndex     <- '[' < [0-9]+ > ']'

        # 函数调用
        # 加 { no_ast_opt }：防止零参数调用（如 cs_size()）被 peglib optimize_ast
        # collapse 成 Identifier，导致 extractAndBuildGraph 漏注册 CS 节点
        FunctionCall    <- Identifier '(' Arguments? ')' { no_ast_opt }
        # Arguments 同样要加 { no_ast_opt }：peglib optimize_ast 会 collapse 掉
        # 只有一个子节点的规则，单参数调用（pct(close) / rank(close) / zscore(close)）
        # 的 Arguments 节点被折叠后 name 变成内层规则名（Unary / ArithExpr ...），
        # 只剩 original_name 还是 "Arguments"。于是按 name == "Arguments" 找参数的
        # 代码（extractAndBuildGraph / evalFunctionCall）认不出单参数调用，
        # 截面函数拿不到自己的表达式 → 评分恒为 NaN。
        # 保留 Arguments 节点后，参数个数与嵌套结构对所有函数都一致可见。
        Arguments       <- Expression (',' Expression)* { no_ast_opt }

        # 数据结构
        ListExpr        <- '[' Expression (',' Expression)* ']'

        # 布尔字面量（支持大小写）
        # %word 指令让 peglib 在匹配这些字面量后自动检查词边界，
        # 避免 true_value 被误解析为 true + _value
        BoolLiteral     <- < 'true' > / < 'false' > / < 'True' > / < 'False' >

        # 标识符和数字
        # Identifier 现在支持带点变量名（如 profit.roe_avg、grp.score），
        # 用于引用 FundamentalNode 和 EMDNode 等发布的带点 context key。
        # %word 指令让 peglib 在匹配关键字（not/and/or/true/false）后自动检查
        # 词边界，避免 notes 被误解析为 not + es、order_flow 被误解析为 or + der_flow
        Identifier      <- < [a-zA-Z_][a-zA-Z_0-9]* ('.' [a-zA-Z_][a-zA-Z_0-9]*)* >
        Number          <- < '-'? [0-9]+ ('.' [0-9-9]+)? >
        String          <- < '"' [^"]* '"' > / < "'" [^']* "'" >

        # 运算符定义
        CompareOp       <- '<=' / '>=' / '==' / '!=' / '<' / '>'
        AddOp           <- '+' / '-'
        MulOp           <- '*' / '@' / '/' / '//' / '%'

        # 语句分隔符
        EOL             <- ';' [ \t\r\n]* / !.
        %word           <- [a-zA-Z_0-9]
        %whitespace     <- [ \t]*
    )";

// 辅助：统一将 context_t 转为 double（支持 double/uint64_t/bool/String 等）
// PEG 解析整数字面量（如 0, 1）可能返回 uint64_t，比较结果返回 bool
// 文件级函数，供 comparationMap 和 arithmeticMap 共用
static double ctxToDoubleArith(const context_t& v) {
    if (auto* p = std::get_if<double>(&v))     return *p;
    if (auto* p = std::get_if<bool>(&v))       return *p ? 1.0 : 0.0;
    if (auto* p = std::get_if<uint64_t>(&v))   return static_cast<double>(*p);
    if (auto* p = std::get_if<Vector<double>>(&v)) return p->empty() ? 0.0 : p->back();
    if (auto* p = std::get_if<String>(&v)) {
        try { return std::stod(*p); } catch (...) { return 0.0; }
    }
    return 0.0;
}

Map<String, std::function<bool (const context_t& , const context_t& )>>& comparationMap() {
    static Map<String, std::function<bool (const context_t& , const context_t& )>> m{
    {">", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l > ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '>' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] > 0'. "
                           "This error should have been caught during validation.");
        }
        THROW_EXCEPTION("Runtime type error: Comparison '>' not supported for type index: {}", left.index());
    }},
    {"<", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l < ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '<' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] < 0'.");
        }
        THROW_EXCEPTION("Runtime type error: Comparison '<' not supported for type index: {}", left.index());
    }},
    {"==", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l == ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '==' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] == 0'.");
        }
        // 支持 bool/uint64_t 等非 double 标量类型的比较
        return ctxToDoubleArith(left) == ctxToDoubleArith(right);
    }},
    {"!=", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l != ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '!=' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] != 0'.");
        }
        return ctxToDoubleArith(left) != ctxToDoubleArith(right);
    }},
    {">=", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l >= ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '>=' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] >= 0'.");
        }
        return ctxToDoubleArith(left) >= ctxToDoubleArith(right);
    }},
    {"<=", [](const context_t& left, const context_t& right) {
        auto l = std::get_if<double>(&left);
        if (l) return *l <= ctxToDoubleArith(right);
        auto lv = std::get_if<Vector<double>>(&left);
        if (lv) {
            THROW_EXCEPTION("Runtime type error: 'Vector<double>' cannot be used with '<=' operator. "
                           "Use [t] or [t-N] to access specific value, e.g., 'MA_5[t] <= 0'.");
        }
        return ctxToDoubleArith(left) <= ctxToDoubleArith(right);
    }},
};
    return m;
}

Map<char, std::function<context_t(const context_t& , const context_t&)>>& arithmeticMap() {
    static Map<char, std::function<context_t(const context_t& , const context_t&)>> m{
    {'+', [](const context_t& left, const context_t& right) {
        return ctxToDoubleArith(left) + ctxToDoubleArith(right);
    }},
    {'-', [](const context_t& left, const context_t& right) {
        return ctxToDoubleArith(left) - ctxToDoubleArith(right);
    }},
    {'*', [](const context_t& left, const context_t& right) {
        return ctxToDoubleArith(left) * ctxToDoubleArith(right);
    }},
    {'/', [](const context_t& left, const context_t& right) {
        double r = ctxToDoubleArith(right);
        if (std::abs(r) < 1e-10 || std::isnan(r)) {
            // WARN("Division by zero / NaN detected for symbol: {}", r);
            return std::nan("nan");
        } else { [[likely]]
            return ctxToDoubleArith(left) / r;
        }
    }},
};
    return m;
}

bool check_bool(const context_t& feature) {
    bool result = false;
    std::visit([&result](auto&& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, double>) {
            if (val != 0) result = true;
        }
        else if constexpr (std::is_same_v<T, bool>) {
            result = val;
        }
    }, feature);
    return result;
}

} // anonymous namespace
