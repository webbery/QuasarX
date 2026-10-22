#pragma once
#include "std_header.h"

enum class TradeAction: char;

// 执行计划条目
struct ExecutionItem {
    symbol_t     _symbol;         // 交易标的
    TradeAction  _action;         // BUY / SELL / HOLD
    int          _quantity;       // 数量
    double       _limitPrice;     // 限价 (0=市价)
    double       _targetValue;    // 目标金额
    unsigned char _flag : 1 = 0;  // 0=开仓, 1=平仓
    bool         _isEmergencyClose = false;  // 紧急平仓标记（风控触发）
};

// 执行计划
struct ExecutionPlan {
    Vector<ExecutionItem> _items;     // 执行条目列表
    double                _totalCapital;  // 总资金
    double                _usedCapital;   // 已用资金
    bool                  _hasChanged;    // 是否有变化
    uint64_t              _generatedAt = 0;  // 生成时的 epoch
    String                _source;           // "live" | "backtest"
};
