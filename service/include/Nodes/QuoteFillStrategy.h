#pragma once
#include "std_header.h"
#include "StrategyNode.h"
#include "Bridge/exchange.h"
#include <functional>

class DataContext;

/**
 * @brief 行情数据缺失填充策略接口
 * 
 * 职责：在多标的时间不对齐时，决定如何填充缺失数据
 * 
 * 设计原则：
 * - 开闭原则：新增填充模式只需新增子类，不修改现有代码
 * - 单一职责：每个策略只负责一种填充逻辑
 */
class IQuoteFillStrategy {
public:
    virtual ~IQuoteFillStrategy() = default;
    
    /**
     * @brief 策略名称（用于日志）
     */
    virtual const char* name() const = 0;
    
    /**
     * @brief 对齐检查 + 数据填充
     * @param quotes 所有标的当前 quote（来自 stepForward）
     * @param minTime 最小时间戳
     * @param lastQuotes 上一次写入的 quote（用于填充参考）
     * @param context 数据上下文
     * @param writeFn 写入函数回调
     * @return true = 成功对齐并写入，false = 跳过
     */
    virtual bool alignAndWrite(
        const Map<symbol_t, QuoteInfo>& quotes,
        time_t minTime,
        const Map<symbol_t, QuoteInfo>& lastQuotes,
        DataContext& context,
        std::function<void(const QuoteInfo&)> writeFn
    ) const = 0;
};

// ============================================================
// Skip 策略 — 不对齐时跳过，不写入任何数据
// ============================================================
class SkipFillStrategy : public IQuoteFillStrategy {
public:
    const char* name() const override { return "Skip"; }
    
    bool alignAndWrite(
        const Map<symbol_t, QuoteInfo>& quotes,
        time_t minTime,
        const Map<symbol_t, QuoteInfo>& lastQuotes,
        DataContext& context,
        std::function<void(const QuoteInfo&)> writeFn
    ) const override;
};

// ============================================================
// ForwardFill 策略 — 前向填充，用上一个已知值填充
// ============================================================
class ForwardFillStrategy : public IQuoteFillStrategy {
public:
    const char* name() const override { return "ForwardFill"; }
    
    bool alignAndWrite(
        const Map<symbol_t, QuoteInfo>& quotes,
        time_t minTime,
        const Map<symbol_t, QuoteInfo>& lastQuotes,
        DataContext& context,
        std::function<void(const QuoteInfo&)> writeFn
    ) const override;
};

// ============================================================
// Linear 策略 — 线性插值，用前后 bar 插值补齐
// ============================================================
class LinearFillStrategy : public IQuoteFillStrategy {
public:
    const char* name() const override { return "Linear"; }
    
    bool alignAndWrite(
        const Map<symbol_t, QuoteInfo>& quotes,
        time_t minTime,
        const Map<symbol_t, QuoteInfo>& lastQuotes,
        DataContext& context,
        std::function<void(const QuoteInfo&)> writeFn
    ) const override;

private:
    /**
     * @brief 线性插值计算
     */
    QuoteInfo interpolate(const QuoteInfo& last, const QuoteInfo& next, time_t target) const;
};

// ============================================================
// BackwardFill 策略 — 后向填充，用下一个已知值填充
// ============================================================
class BackwardFillStrategy : public IQuoteFillStrategy {
public:
    const char* name() const override { return "BackwardFill"; }
    
    bool alignAndWrite(
        const Map<symbol_t, QuoteInfo>& quotes,
        time_t minTime,
        const Map<symbol_t, QuoteInfo>& lastQuotes,
        DataContext& context,
        std::function<void(const QuoteInfo&)> writeFn
    ) const override;
};

/**
 * @brief 工厂函数：根据 MissingHandleType 创建对应策略
 */
std::unique_ptr<IQuoteFillStrategy> CreateFillStrategy(MissingHandleType type);
