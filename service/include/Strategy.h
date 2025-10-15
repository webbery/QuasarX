#pragma once
#include "std_header.h"
#include "StrategyNode.h"

#define BASIC_NAME  "Basic"

enum class ContractOperator: unsigned char {
  Hold = 0,
  Buy = 1,
  Sell = 2,
  Short = 4,
  Done = (0x1<<7),
};

template <>
struct fmt::formatter<ContractOperator> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin(); // 简单情况直接返回
    }

    template <typename FormatContext>
    auto format(ContractOperator op, FormatContext& ctx) const {
        String info;
        switch (op)
        {
        case ContractOperator::Hold: info = "Hold"; break;
        case ContractOperator::Buy: info = "Buy"; break;
        case ContractOperator::Sell: info = "Sell"; break;
        case ContractOperator::Short: info = "Short"; break;
        case ContractOperator::Done: info = "Done"; break;
        default: break;
        }
        return fmt::format_to(ctx.out(), "{}", info);
    }
};

enum class StrategyType: char {
    ST_Unknow,
    ST_InterDay,
    ST_IntraDay,
    ST_Count,
};

enum class PredictType: char {
    PT_UpDown,
    
};
class IStrategy {
public:
    virtual ~IStrategy() {}
    virtual int generate(const Vector<float>& prediction) = 0;
    virtual bool is_valid() = 0;
};

enum class StrategyNodeType {
    Input,
    Operation,
    Function,
    Output,
    Feature
};

class QStrategy: public QNode {
public:
    QStrategy();

    virtual List<QNode*> Process(const List<QNode*>& input);

    void setT0(bool yes) { _isT0 = yes; }
    bool isT0() { return _isT0; }

protected:
    bool _isT0;
};

struct AgentStrategyInfo;
// AgentStrategyInfo parse_strategy_script(const nlohmann::json& content);

List<QNode*> parse_strategy_script_v2(const nlohmann::json& content);
// 对输入的有向图节点作topo排序，返回起始节点
QNode* topo_sort(const List<QNode*>& graph);
