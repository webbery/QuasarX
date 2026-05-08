#pragma once
#include "std_header.h"

/**
 * @brief 风控触发类型枚举
 */
enum class RiskTriggerType {
    None,
    StopLoss,       // 固定百分比止损
    TakeProfit,     // 固定百分比止盈
    TrailingStop,   // 追踪止损（移动止盈）
    TimeStop        // 时间止损（超时平仓）
};

/**
 * @brief 风控动作枚举
 */
enum class RiskAction {
    None,
    Close,          // 全部平仓
    Reduce          // 减仓（配合 reduce_ratio 使用）
};

/**
 * @brief 将触发类型转为字符串（用于日志）
 */
inline const char* to_string(RiskTriggerType t) {
    switch (t) {
        case RiskTriggerType::None:         return "none";
        case RiskTriggerType::StopLoss:     return "stop_loss";
        case RiskTriggerType::TakeProfit:   return "take_profit";
        case RiskTriggerType::TrailingStop: return "trailing_stop";
        case RiskTriggerType::TimeStop:     return "time_stop";
    }
    return "unknown";
}

/**
 * @brief 将动作类型转为字符串（用于日志）
 */
inline const char* to_string(RiskAction a) {
    switch (a) {
        case RiskAction::None:   return "none";
        case RiskAction::Close:  return "close";
        case RiskAction::Reduce: return "reduce";
    }
    return "unknown";
}

/**
 * @brief 风控上下文
 *
 * 由 Strategy 持有，用于在策略节点执行流中传递风控短路信号。
 * 独立于 DataContext，不污染节点间的数据通道。
 *
 * 使用方式：
 *   1. ProtectionNode 检查持仓状态，触发风控时写入 triggered/trigger_type/action
 *   2. 后续中间节点检查 triggered，为 true 时直接返回 Skip
 *   3. ExecutionNode 检查 triggered，为 true 时执行紧急平仓
 *   4. 每轮新 Bar 开始时由 ProtectionNode 调用 reset() 重置
 */
class RiskContext {
public:
    bool triggered = false;
    RiskTriggerType trigger_type = RiskTriggerType::None;
    RiskAction action = RiskAction::None;
    double reduce_ratio = 0.0;      // [0, 1]，仅 action=Reduce 时有效

    void reset() {
        triggered = false;
        trigger_type = RiskTriggerType::None;
        action = RiskAction::None;
        reduce_ratio = 0.0;
    }
};
