#pragma once

#include "Guard.h"
#include "Util/log.h"

/**
 * @brief 固定百分比止损
 * 触发条件: 多头 current_price <= entry_price * (1 - percent)
 *           空头 current_price >= entry_price * (1 + percent)
 */
class StopLossGuard : public Guard {
public:
    explicit StopLossGuard(double percent = 0.08) : _percent(percent) {
        _enabled = true;
    }

    RiskTriggerType check(symbol_t symbol, DataContext& context,
                          const EntryInfo& entry_info) override {
        if (!_enabled || entry_info._avg_price <= 0) {
            return RiskTriggerType::None;
        }

        double current_price = getCurrentPrice(symbol, context);
        if (current_price <= 0) return RiskTriggerType::None;

        // 空头方向取反：价格上涨才是亏损，止损线在成本价上方
        double sl_price = entry_info._isShort
            ? entry_info._avg_price * (1.0 + _percent)
            : entry_info._avg_price * (1.0 - _percent);
        const bool hit = entry_info._isShort
            ? (current_price >= sl_price)
            : (current_price <= sl_price);
        if (hit) {
            INFO("[Guard:StopLoss] TRIGGERED! symbol={} {} entry={:.2f} current={:.2f} sl_price={:.2f}",
                 get_symbol(symbol), entry_info._isShort ? "SHORT" : "LONG",
                 entry_info._avg_price, current_price, sl_price);
            return RiskTriggerType::StopLoss;
        }
        return RiskTriggerType::None;
    }

    String name() const override { return "StopLoss"; }

    void setPercent(double percent) { _percent = percent; }
    double getPercent() const { return _percent; }

private:
    double _percent;  // 止损比例，如 0.08 = 8%
};

/**
 * @brief 止盈
 * 触发条件: 多头 current_price >= entry_price * (1 + percent)
 *           空头 current_price <= entry_price * (1 - percent)
 */
class TakeProfitGuard : public Guard {
public:
    explicit TakeProfitGuard(double percent = 0.05) : _percent(percent) {
        _enabled = true;
    }

    RiskTriggerType check(symbol_t symbol, DataContext& context,
                          const EntryInfo& entry_info) override {
        if (!_enabled || entry_info._avg_price <= 0) {
            return RiskTriggerType::None;
        }

        double current_price = getCurrentPrice(symbol, context);
        if (current_price <= 0) return RiskTriggerType::None;

        // 空头方向取反：价格下跌才是盈利，止盈线在成本价下方
        double tp_price = entry_info._isShort
            ? entry_info._avg_price * (1.0 - _percent)
            : entry_info._avg_price * (1.0 + _percent);
        const bool hit = entry_info._isShort
            ? (current_price <= tp_price)
            : (current_price >= tp_price);
        if (hit) {
            INFO("[Guard:TakeProfit] TRIGGERED! symbol={} {} entry={:.2f} current={:.2f} tp_price={:.2f}",
                 get_symbol(symbol), entry_info._isShort ? "SHORT" : "LONG",
                 entry_info._avg_price, current_price, tp_price);
            return RiskTriggerType::TakeProfit;
        }
        return RiskTriggerType::None;
    }

    String name() const override { return "TakeProfit"; }

    void setPercent(double percent) { _percent = percent; }

private:
    double _percent;
};

/**
 * @brief 追踪止损（Trailing Stop）
 * 触发条件: 多头 current_price <= highest_price * (1 - percent)
 *           空头 current_price >= lowest_price  * (1 + percent)
 * 
 * 与 StopLoss 的区别：
 * - StopLoss: 固定止损线（多头 entry*(1-percent)，空头 entry*(1+percent)）
 * - TrailingStop: 动态止损线，多头跟随最高价、空头跟随最低价
 */
class TrailingStopGuard : public Guard {
public:
    explicit TrailingStopGuard(double percent = 0.03) : _percent(percent) {
        _enabled = true;
    }

    RiskTriggerType check(symbol_t symbol, DataContext& context,
                          const EntryInfo& entry_info) override {
        if (!_enabled || entry_info._avg_price <= 0) {
            return RiskTriggerType::None;
        }

        double current_price = getCurrentPrice(symbol, context);
        if (current_price <= 0) return RiskTriggerType::None;

        if (entry_info._isShort) {
            // 空头：最低价回撤（上涨）触发止损
            double lowest = entry_info._lowest_price > 0
                ? entry_info._lowest_price : entry_info._avg_price;
            double ts_price = lowest * (1.0 + _percent);
            if (current_price >= ts_price) {
                INFO("[Guard:TrailingStop] TRIGGERED! symbol={} SHORT lowest={:.2f} current={:.2f} ts_price={:.2f}",
                     get_symbol(symbol), lowest, current_price, ts_price);
                return RiskTriggerType::TrailingStop;
            }
            return RiskTriggerType::None;
        }

        // 使用持仓期间的最高价（复权口径）
        double highest = entry_info._highest_price > 0 ? entry_info._highest_price : entry_info._avg_price;
        double ts_price = highest * (1.0 - _percent);
        
        if (current_price <= ts_price) {
            INFO("[Guard:TrailingStop] TRIGGERED! symbol={} LONG highest={:.2f} current={:.2f} ts_price={:.2f}",
                 get_symbol(symbol), highest, current_price, ts_price);
            return RiskTriggerType::TrailingStop;
        }
        return RiskTriggerType::None;
    }

    void update(symbol_t symbol, DataContext& context, EntryInfo& entry_info) override {
        // 更新极值（无论是否触发都要更新）：多头跟踪最高价，空头跟踪最低价
        double current_price = getCurrentPrice(symbol, context);
        if (current_price > 0) {
            if (entry_info._isShort) {
                if (entry_info._lowest_price <= 0 || current_price < entry_info._lowest_price) {
                    entry_info._lowest_price = current_price;
                }
            } else if (current_price > entry_info._highest_price) {
                entry_info._highest_price = current_price;
            }
        }
    }

    String name() const override { return "TrailingStop"; }

    void setPercent(double percent) { _percent = percent; }

private:
    double _percent;
};

/**
 * @brief 时间止损
 * 触发条件: 持仓 bar 数 >= max_bars
 */
class TimeStopGuard : public Guard {
public:
    explicit TimeStopGuard(int max_bars = 10) : _maxBars(max_bars) {
        _enabled = true;
    }

    RiskTriggerType check(symbol_t symbol, DataContext& context,
                          const EntryInfo& entry_info) override {
        if (!_enabled) return RiskTriggerType::None;

        int current_bar = context.GetEpoch();
        int held_bars = current_bar - entry_info._entry_bar;
        
        if (held_bars >= _maxBars) {
            INFO("[Guard:TimeStop] TRIGGERED! symbol={} held_bars={} max_bars={}",
                 get_symbol(symbol), held_bars, _maxBars);
            return RiskTriggerType::TimeStop;
        }
        return RiskTriggerType::None;
    }

    String name() const override { return "TimeStop"; }

    void setMaxBars(int max_bars) { _maxBars = max_bars; }

private:
    int _maxBars;
};