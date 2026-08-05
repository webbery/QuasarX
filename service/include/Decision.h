#pragma once
#include "std_header.h"
#include "Util/system.h"
#include <cstdint>

enum class TradeAction : char;

// 决策操作类型：合并 action(BUY/SELL) + flag(开/平) 为单一枚举，2 bit
enum class DecisionAction : uint8_t {
    OpenLong   = 0,  // 买入开多  (BUY,  flag=0)
    CloseLong  = 1,  // 卖出平多  (SELL, flag=1)
    OpenShort  = 2,  // 卖出开空  (SELL, flag=0)
    CloseShort = 3,  // 买入平空  (BUY,  flag=1)
};

// 转换函数（实现在 Decision.cpp）
DecisionAction to_decision_action(TradeAction action, unsigned char flag);
TradeAction decision_to_action(DecisionAction da);
unsigned char decision_to_flag(DecisionAction da);

inline const char* decision_action_label(DecisionAction da) {
    switch (da) {
        case DecisionAction::OpenLong:   return "买入开多";
        case DecisionAction::CloseLong:  return "卖出平多";
        case DecisionAction::OpenShort:  return "卖出开空";
        case DecisionAction::CloseShort: return "买入平空";
    }
    return "未知";
}

inline const char* decision_action_name(DecisionAction da) {
    switch (da) {
        case DecisionAction::OpenLong:   return "open_long";
        case DecisionAction::CloseLong:  return "close_long";
        case DecisionAction::OpenShort:  return "open_short";
        case DecisionAction::CloseShort: return "close_short";
    }
    return "unknown";
}

// 决策记录（位域优化内存布局）
struct DecisionRecord {
    int32_t     _id;
    symbol_t    _symbol;              // 8 bytes packed
    int32_t     _epoch;
    int64_t     _quantity;
    int64_t     _executed_quantity;
    double      _price;
    double      _executed_price;
    time_t      _timestamp;

    // 位域打包: action(2) + is_open(1) + executed(1) + reserved(4) = 1 byte
    DecisionAction _action   : 2;
    bool           _is_open  : 1;
    bool           _executed : 1;
    uint8_t        _reserved : 4;

    char _strategy[32];
};
