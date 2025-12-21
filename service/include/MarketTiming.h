#pragma once 
#include "std_header.h"

struct ExecutionDecision {
    enum class Action {
        ACT_MARKET_BUY,      // 市价买入
        ACT_MARKET_SELL,     // 市价卖出
        ACT_LIMIT_BUY,       // 限价买入
        ACT_LIMIT_SELL,      // 限价卖出
        ACT_CANCEL,          // 取消订单
        ACT_WAIT,            // 等待
        ACT_IGNORE           // 忽略信号
    };

    Action _action;
    double _price;       // 对于限价单
    double _quantity;    // 数量
    String _reason; // 决策原因
};

class TradeSignal;
class DataContext;
class ITimingStrategy {
public:
    virtual ~ITimingStrategy(){}

    virtual ExecutionDecision processSignal(const TradeSignal& signal, const DataContext& context) = 0;
};
