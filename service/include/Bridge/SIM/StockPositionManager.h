#pragma once
#include "Bridge/exchange.h"
#include <mutex>

// 股票模拟持仓信息
struct StockPosition {
    symbol_t _symbol;
    int64_t _qty = 0;          // 持仓数量（正=多头，负=空头）
    double _cost = 0.0;        // 持仓成本（均价）
    double _totalCost = 0.0;   // 总成本 = qty * cost
};

/**
 * @brief 股票持仓管理器
 * 
 * 设计目标：
 * 1. 单一职责：只负责持仓增减、成本计算、资金扣减
 * 2. 可复用：回测/实盘仿真/TickFlowBridge 共用同一逻辑
 * 3. 可配置：支持佣金/印花税设置，支持回测模式（不扣资金）
 */
class StockPositionManager {
public:
    StockPositionManager(double initialCapital = 500000.0);
    ~StockPositionManager() = default;

    // === 持仓操作 ===

    /// @brief 买入（增加多头持仓，扣减资金）
    void Buy(symbol_t symbol, int64_t qty, double price);

    /// @brief 卖出（减少多头持仓，增加资金）
    void Sell(symbol_t symbol, int64_t qty, double price);

    /// @brief 调整持仓（通用：delta > 0 买入，delta < 0 卖出）
    void AdjustPosition(symbol_t symbol, int64_t delta, double price);

    // === 查询 ===

    /// @brief 获取某标的持仓数量
    int64_t GetPosition(symbol_t symbol) const;

    /// @brief 设置持仓数量（直接覆盖，回测初始化用）
    void SetPosition(symbol_t symbol, int64_t qty);

    /// @brief 获取某标的持仓成本（均价）
    double GetPositionCost(symbol_t symbol) const;

    /// @brief 获取可用资金
    double GetAvailableFunds() const;

    /// @brief 获取初始资金
    double getCapital() const { return _initialCapital; }

    /// @brief 获取账户资产
    AccountAsset GetAsset() const;

    /// @brief 获取所有持仓
    bool GetPosition(AccountPosition& pos) const;

    // === 配置 ===

    /// @brief 设置佣金费率（默认 0.0003）
    void SetCommission(double rate) { _commissionRate = rate; }

    /// @brief 设置印花税率（默认 0.001，仅卖出收取）
    void SetStampTax(double rate) { _stampTaxRate = rate; }

    /// @brief 设置初始资金
    void SetInitialCapital(double capital);

    /// @brief 回测模式：不扣资金，只记持仓
    void SetBacktestMode(bool bt) { _backtestMode = bt; }
    bool IsBacktestMode() const { return _backtestMode; }

    /// @brief 重置（用于策略重启）
    void Reset();

private:
    /// @brief 计算交易费用（佣金 + 印花税）
    double CalcFees(double amount, bool isSell) const;

    mutable std::mutex _mutex;
    Map<symbol_t, StockPosition> _positions;
    double _availableFunds;
    double _initialCapital;
    double _commissionRate;  // 佣金费率
    double _stampTaxRate;    // 印花税率（仅卖出）
    bool _backtestMode;      // 回测模式标志
};
