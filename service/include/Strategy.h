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
    Portfolio,  // 投资组合节点（含仓位管理）
    Feature,
    Script,
    LSTM,
    BOOST,
    NARX,
    Debug,
    Stack,
    Test,
    Spread,     // 价差计算节点（配对交易）
    Protection, // 风控保护节点（止损/止盈/追踪/时间）
    EMD,        // 经验模态分解节点（信号处理）
    HMM,        // 隐马尔可夫节点（因果推理：市场状态识别）
    Resample,   // 数据重采样节点（高频→低频聚合）
    Formula,    // 公式计算节点（自定义表达式计算）
};

struct AgentStrategyInfo;
// AgentStrategyInfo parse_strategy_script(const nlohmann::json& content);

/**
 * @brief 滑点配置结构（从 ExecuteNode 提取）
 */
struct SlippageConfigInfo {
    Set<String> sources;          // 策略使用的数据源集合
    nlohmann::json modelConfig;   // 滑点模型 JSON 配置
};

class Server;
class QNode;

/**
 * @brief 解析策略图 v2 版本
 * @param content 策略 JSON 内容
 * @param server Server 指针
 * @param outSlippageConfig 可选输出参数，用于返回滑点配置
 * @param outNodeConfigMap 可选输出参数，用于返回节点配置映射（用于后续初始化）
 * @return 解析后的节点列表
 */
List<QNode*> parse_strategy_script_v2(
    const nlohmann::json& content,
    Server* server,
    SlippageConfigInfo* outSlippageConfig = nullptr,
    std::map<uint32_t, nlohmann::json>* outNodeConfigMap = nullptr
);
// 对输入的有向图节点作topo排序，返回排序后的节点
List<QNode*> topo_sort(const List<QNode*>& graph);
