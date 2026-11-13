#pragma once

#include "DataContext.h"
#include "RiskContext.h"

/**
 * @brief Guard 抽象基类 - 风控保护策略
 *
 * 设计目标：
 * 1. 将止损/止盈等风控策略抽象为独立的 Guard 类
 * 2. 支持按需组合、动态创建
 * 3. 解耦 ProtectionNode 的执行时机问题
 *
 * 使用方式：
 * - ProtectionNode 持有 Vector<Guard*> _guards
 * - 根据配置动态 new 对应子类
 * - Process 时遍历 _guards 调用 check()
 */
class Guard {
public:
    virtual ~Guard() = default;

    /**
     * @brief 检查是否触发风控
     * @param symbol 标的
     * @param context 数据上下文
     * @param entry_info 入场信息（avg_price, highest_price, lowest_price, entry_bar）
     * @return RiskTriggerType::None 表示未触发，其他表示触发的类型
     */
    virtual RiskTriggerType check(symbol_t symbol, DataContext& context,
                                   const struct EntryInfo& entry_info) = 0;

    /**
     * @brief 更新内部状态（如追踪止损的最高价）
     * @note 每根 bar 都调用，无论是否触发
     */
    virtual void update(symbol_t symbol, DataContext& context,
                        struct EntryInfo& entry_info) {}

    /**
     * @brief 获取 Guard 名称（用于日志）
     */
    virtual String name() const = 0;

    /**
     * @brief 是否启用
     */
    bool enabled() const { return _enabled; }
    void setEnabled(bool enabled) { _enabled = enabled; }

protected:
    bool _enabled = false;

    // 辅助函数：获取当前收盘价
    double getCurrentPrice(symbol_t symbol, DataContext& context) {
        String close_key = get_symbol(symbol) + ".close";
        if (context.exist(close_key)) {
            auto price_var = context.get<Vector<double>>(close_key);
            if (!price_var.empty()) {
                return price_var.back();
            }
        }
        return 0.0;
    }
};

/**
 * @brief 入场信息结构体（与 ProtectionNode::EntryInfo 保持一致）
 */
struct EntryInfo {
    double _avg_price = 0.0;       // 入场均价（复权口径）
    double _raw_cost = 0.0;        // 入场成本（原始价）
    double _highest_price = 0.0;   // 持仓期间最高价（复权口径）
    double _lowest_price = 0.0;    // 持仓期间最低价（复权口径）
    int    _entry_bar = 0;         // 入场 Bar 索引
};