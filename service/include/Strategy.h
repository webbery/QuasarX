#pragma once
#include "std_header.h"
#include "json.hpp"

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
    Unknow,
    Input,      // 输入节点
    Function,
    Signal,
    Execution,
    Feature,
    Script,
    LSTM,
    BOOST,
    NARX,
    Debug,
    Stack,
};

struct AgentStrategyInfo;
// AgentStrategyInfo parse_strategy_script(const nlohmann::json& content);

class Server;
class QNode;
List<QNode*> parse_strategy_script_v2(const nlohmann::json& content, Server* server);
// 对输入的有向图节点作topo排序，返回排序后的节点
List<QNode*> topo_sort(const List<QNode*>& graph);
