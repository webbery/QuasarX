#pragma once
#include "std_header.h"
#include <cmath>

// ═══ 持仓事件效果（统一描述任何事件对持仓/现金的影响）═══
struct PositionEffect {
    int64_t qtyDelta = 0;        // 持仓数量变化（送股+300、到期-1000）
    double cashDelta = 0.0;      // 现金变化（分红+200、移仓价差-50）
    double costBasisDelta = 0.0; // 总成本变化（配股缴款等）
};

// ═══ 持仓事件接口 ═══
//
// 所有"外部强制的持仓状态转换"都实现此接口：
//   - 股票分红/送股/转增
//   - 期货合约到期（未来）
//   - 可转债转股（未来）
//
// 核心原则：被动事件不走 Buy/Sell 路径，不产生佣金/印花税
class IPositionEvent {
public:
    virtual ~IPositionEvent() = default;

    /// 事件触发日期
    virtual time_t triggerDate() const = 0;

    /// 事件涉及的标的
    virtual symbol_t symbol() const = 0;

    /// 计算事件效果
    /// @param currentQty  当前持仓数量
    /// @param currentPrice 当前原始价格（org_close）
    virtual PositionEffect computeEffect(int64_t currentQty, double currentPrice) const = 0;
};

// ═══ 股票分红/送股/转增事件 ═══
//
// 数据来源: FinanceDB.dividend 表（BaoStock）
// 不处理配股（allot），配股需要股东主动缴款，第一版跳过
class StockDividendEvent : public IPositionEvent {
    time_t _exDate;
    symbol_t _symbol;
    double _bonusPerShare;     // 每股送股数 (bonus_per_10 / 10)
    double _transferPerShare;  // 每股转增数 (transfer_per_10 / 10)
    double _cashPerShare;      // 每股派息元 (cash_per_10 / 10)

public:
    StockDividendEvent(time_t exDate, symbol_t symbol,
                       double bonusPer10, double transferPer10, double cashPer10)
        : _exDate(exDate), _symbol(symbol)
        , _bonusPerShare(bonusPer10 / 10.0)
        , _transferPerShare(transferPer10 / 10.0)
        , _cashPerShare(cashPer10 / 10.0)
    {}

    time_t triggerDate() const override { return _exDate; }
    symbol_t symbol() const override { return _symbol; }

    PositionEffect computeEffect(int64_t currentQty, double /*price*/) const override {
        PositionEffect fx;
        if (currentQty <= 0) return fx;  // 空头或无持仓不处理

        // 送股/转增 → 数量增加（向下取整，A 股零股也入账）
        double stockRatio = _bonusPerShare + _transferPerShare;
        if (stockRatio > 0) {
            fx.qtyDelta = static_cast<int64_t>(std::floor(currentQty * stockRatio));
        }

        // 现金分红 → 到账金额
        if (_cashPerShare > 0) {
            fx.cashDelta = currentQty * _cashPerShare;
        }

        return fx;
    }
};
