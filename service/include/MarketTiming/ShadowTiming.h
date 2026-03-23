#pragma once
#include "MarketTiming.h"
#include "DataContext.h"
#include "Bridge/exchange.h"
#include <fstream>
#include <mutex>
#include <atomic>

// 影子模式配置
struct ShadowConfig {
    String logPath;           // 日志文件路径
    double slippageRate;      // 滑点比例 (如 0.001 表示 0.1%)
    double initialCapital;    // 初始虚拟资金
    bool estimateFill;        // 是否估算成交
    ShadowConfig()
        : logPath("logs/shadow/"), slippageRate(0.001), initialCapital(100000.0), estimateFill(true) {}
};

// 影子模式下的模拟成交结果
struct ShadowFillResult {
    bool filled;              // 是否成交
    double fillPrice;         // 模拟成交价
    int64_t fillQty;          // 模拟成交数量
    String status;            // 状态：FILLED/PENDING/REJECTED

    ShadowFillResult() : filled(false), fillPrice(0), fillQty(0), status("PENDING") {}
};

/**
 * 影子账户 - 跟踪虚拟资金和持仓
 */
class ShadowAccount {
public:
    ShadowAccount(double initialCapital);

    // 尝试冻结虚拟资金（买入时）
    bool TryFreezeFunds(double cost);

    // 释放虚拟资金（卖出成交后）
    void ReleaseFunds(double amount);

    // 更新持仓
    void UpdatePosition(symbol_t symbol, int64_t delta);

    // 获取持仓
    int64_t GetPosition(symbol_t symbol) const;

    // 获取可用资金
    double GetAvailableFunds() const { return virtualAvailable.load(); }

    // 获取总资产
    double GetTotalAsset() const { return virtualCapital.load(); }

private:
    std::atomic<double> virtualCapital;     // 虚拟总资产
    std::atomic<double> virtualAvailable;   // 虚拟可用资金
    Map<symbol_t, int64_t> shadowPositions; // 影子持仓
    mutable std::mutex accountMutex;
};

/**
 * 影子模式定时策略
 *
 * 功能：
 * 1. 接收交易信号但不实际下单
 * 2. 从 QuoteNode 订阅实时行情用于成交估算
 * 3. 记录信号详情到日志文件
 * 4. 基于历史行情估算模拟成交
 */
class ShadowTiming : public ITimingStrategy {
public:
    ShadowTiming(Server* server, const ShadowConfig& config = ShadowConfig());
    ~ShadowTiming();

    /**
     * 处理交易信号
     * - 从 DataContext 获取当前 Bar 数据
     * - 记录信号到日志
     * - 估算模拟成交
     */
    virtual bool processSignal(const String& strategy, const TradeSignal& signal,
                               const DataContext& context) override;

private:
    /**
     * 估算限价单成交
     * @param signal 交易信号
     * @param bar 当前 K 线行情
     * @return 成交结果
     */
    ShadowFillResult EstimateLimitFill(const TradeSignal& signal, const QuoteInfo& bar);

    /**
     * 估算市价单成交
     * @param signal 交易信号
     * @param bar 当前 K 线行情
     * @return 成交结果
     */
    ShadowFillResult EstimateMarketFill(const TradeSignal& signal, const QuoteInfo& bar);

    /**
     * 记录影子日志
     */
    void LogShadowSignal(const String& strategy, const TradeSignal& signal,
                         const ShadowFillResult& fillResult);

    /**
     * 初始化日志文件
     */
    void InitLogFile();

    /**
     * 获取当前日期字符串
     */
    static String GetDateStr();

    /**
     * 从 DataContext 获取当前 Bar 数据
     */
    bool GetCurrentBar(symbol_t symbol, QuoteInfo& bar);

private:
    ShadowConfig _config;
    std::ofstream _logFile;
    std::mutex _logMutex;
    std::unique_ptr<ShadowAccount> _account;  // 影子账户
};
