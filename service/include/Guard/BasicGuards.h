#pragma once

#include "Guard.h"
#include "Util/log.h"

/**
 * @brief 固定百分比止损
 * 触发条件: current_price <= entry_price * (1 - percent)
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

        double sl_price = entry_info._avg_price * (1.0 - _percent);
        if (current_price <= sl_price) {
            INFO("[Guard:StopLoss] TRIGGERED! symbol={} entry={:.2f} current={:.2f} sl_price={:.2f}",
                 get_symbol(symbol), entry_info._avg_price, current_price, sl_price);
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
 * 触发条件: current_price >= entry_price * (1 + percent)
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

        double tp_price = entry_info._avg_price * (1.0 + _percent);
        if (current_price >= tp_price) {
            INFO("[Guard:TakeProfit] TRIGGERED! symbol={} entry={:.2f} current={:.2f} tp_price={:.2f}",
                 get_symbol(symbol), entry_info._avg_price, current_price, tp_price);
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
 * 触发条件: current_price <= highest_price * (1 - percent)
 * 
 * 与 StopLoss 的区别：
 * - StopLoss: 固定止损线（entry_price * (1 - percent)）
 * - TrailingStop: 动态止损线（highest_price * (1 - percent)），跟随最高价上涨
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

        // 使用持仓期间的最高价（复权口径）
        double highest = entry_info._highest_price > 0 ? entry_info._highest_price : entry_info._avg_price;
        double ts_price = highest * (1.0 - _percent);
        
        if (current_price <= ts_price) {
            INFO("[Guard:TrailingStop] TRIGGERED! symbol={} highest={:.2f} current={:.2f} ts_price={:.2f}",
                 get_symbol(symbol), highest, current_price, ts_price);
            return RiskTriggerType::TrailingStop;
        }
        return RiskTriggerType::None;
    }

    void update(symbol_t symbol, DataContext& context, EntryInfo& entry_info) override {
        // 更新最高价（无论是否触发都要更新）
        double current_price = getCurrentPrice(symbol, context);
        if (current_price > 0) {
            if (current_price > entry_info._highest_price) {
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