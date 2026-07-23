#pragma once
#include "Bridge/exchange.h"
#include "Bridge/PositionEvent.h"
#include <mutex>

// 股票模拟持仓信息
struct StockPosition {
    symbol_t _symbol;
    int64_t _qty = 0;          // 持仓数量（正=多头，负=空头）
    double _cost = 0.0;        // 持仓成本（均价）
    double _totalCost = 0.0;   // 总成本 = qty * cost
};

// 交易费用明细（Buy/Sell 返回给调用方，由调用方扣资金）
struct TradeFees {
    double commission = 0.0;   // 佣金
    double stampTax = 0.0;     // 印花税（仅卖出）
    double total() const { return commission + stampTax; }
};

/**
 * @brief 股票持仓管理器（纯持仓管理，不管理资金）
 *
 * 设计目标：
 * 1. 单一职责：只负责持仓增减、成本计算、T+1 控制
 * 2. 可复用：回测/实盘仿真/TickFlowBridge 共用同一逻辑
 * 3. 资金由外部管理：Buy/Sell 返回费用明细，调用方负责扣/加资金
 */
class StockPositionManager {
public:
    StockPositionManager();
    ~StockPositionManager() = default;

    // === 持仓操作 ===

    /// @brief 买入（增加多头持仓，返回费用明细）
    TradeFees Buy(symbol_t symbol, int64_t qty, double price);

    /// @brief 卖出（减少多头持仓，返回费用明细和实际卖出数量）
    struct SellResult {
        TradeFees fees;
        int64_t actualQty = 0;  // 实际卖出数量（可能因 T+1 或持仓不足而减少）
        double proceeds = 0.0;  // 净收入 = amount - fees
    };
    SellResult Sell(symbol_t symbol, int64_t qty, double price);

    /// @brief 调整持仓（通用：delta > 0 买入，delta < 0 卖出）
    TradeFees AdjustPosition(symbol_t symbol, int64_t delta, double price);

    /// @brief 应用持仓事件（分红/送股等被动事件，不走 Buy/Sell 路径）
    /// 不产生佣金/印花税，不触发 T+1
    /// @return 实际效果（调用方负责现金变动）
    PositionEffect ApplyEvent(symbol_t symbol, const IPositionEvent& event, double currentPrice);

    // === 查询 ===

    /// @brief 获取某标的持仓数量
    int64_t GetPosition(symbol_t symbol) const;

    /// @brief 设置持仓数量（直接覆盖，回测初始化用）
    void SetPosition(symbol_t symbol, int64_t qty);

    /// @brief 获取某标的持仓成本（均价）
    double GetPositionCost(symbol_t symbol) const;

    /// @brief 获取账户资产（仅持仓信息，不含资金）
    AccountAsset GetAsset() const;

    /// @brief 获取所有持仓
    bool GetPosition(AccountPosition& pos) const;

    // === 配置 ===

    /// @brief 设置佣金费率（默认 0.0003）
    void SetCommission(double rate) { _commissionRate = rate; }

    /// @brief 设置印花税率（默认 0.001，仅卖出收取）
    void SetStampTax(double rate) { _stampTaxRate = rate; }

    /// @brief 重置（用于策略重启）
    void Reset();

    /// @brief 每日开盘前调用，重置当日买入记录（T+1 控制用）
    void OnDayChange();

private:
    /// @brief 计算交易费用（佣金 + 印花税）
    TradeFees CalcFees(double amount, bool isSell) const;

    mutable std::mutex _mutex;
    Map<symbol_t, StockPosition> _positions;
    double _commissionRate;  // 佣金费率
    double _stampTaxRate;    // 印花税率（仅卖出）

    // T+1 控制：当日买入不能当日卖出
    Map<symbol_t, int64_t> _todayBuyQty;  // 当日买入数量
};
