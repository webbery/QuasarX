#pragma once

#include "Guard.h"
#include "Function/Function.h"
#include "Util/log.h"
#include <memory>

/**
 * @brief ATR 自适应止损
 * 触发条件: current_price <= entry_price - multiplier * ATR(period)
 * 
 * 特点：根据波动率动态调整止损距离
 * 高波动时止损距离大，低波动时止损距离小
 */
class AtrStopLossGuard : public Guard {
public:
    explicit AtrStopLossGuard(int period = 20, double multiplier = 2.0)
        : _period(period), _multiplier(multiplier) {
        _enabled = true;
    }

    RiskTriggerType check(symbol_t symbol, DataContext& context,
                          const EntryInfo& entry_info) override {
        if (!_enabled || entry_info._avg_price <= 0) {
            return RiskTriggerType::None;
        }

        double current_price = getCurrentPrice(symbol, context);
        if (current_price <= 0) return RiskTriggerType::None;

        // 懒初始化 ATR 计算器
        if (!_atr_calc[symbol]) {
            _atr_calc[symbol] = std::make_unique<ATR>(_period);
        }

        // 构造输入参数
        Map<String, context_t> args;
        String prefix = get_symbol(symbol) + ".";
        if (context.exist(prefix + "high"))  args["high"]  = context.get<Vector<double>>(prefix + "high");
        if (context.exist(prefix + "low"))   args["low"]   = context.get<Vector<double>>(prefix + "low");
        if (context.exist(prefix + "close")) args["close"] = context.get<Vector<double>>(prefix + "close");

        context_t result = (*_atr_calc[symbol])(args);
        double atr = std::visit([](const auto& v) -> double {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double>) return v;
            else if constexpr (std::is_same_v<T, Vector<double>>)
                return v.empty() ? std::nan("") : v.back();
            else return std::nan("");
        }, result);

        if (std::isnan(atr) || atr <= 0) return RiskTriggerType::None;

        double sl_price = entry_info._avg_price - _multiplier * atr;
        if (current_price <= sl_price) {
            INFO("[Guard:AtrStopLoss] TRIGGERED! symbol={} entry={:.2f} current={:.2f} atr={:.2f} sl_price={:.2f}",
                 get_symbol(symbol), entry_info._avg_price, current_price, atr, sl_price);
            return RiskTriggerType::AtrStopLoss;
        }
        return RiskTriggerType::None;
    }

    String name() const override { return "AtrStopLoss"; }

    void setParams(int period, double multiplier) {
        _period = period;
        _multiplier = multiplier;
    }

private:
    int _period;
    double _multiplier;
    Map<symbol_t, std::unique_ptr<ATR>> _atr_calc;
};

/**
 * @brief MA 跌破止损
 * 触发条件: current_price <= MA(period)
 * 
 * 特点：趋势策略常用，跌破均线视为趋势结束
 */
class MaStopLossGuard : public Guard {
public:
    explicit MaStopLossGuard(int period = 20) : _period(period) {
        _enabled = true;
    }

    RiskTriggerType check(symbol_t symbol, DataContext& context,
                          const EntryInfo& entry_info) override {
        if (!_enabled) return RiskTriggerType::None;

        double current_price = getCurrentPrice(symbol, context);
        if (current_price <= 0) return RiskTriggerType::None;

        if (!_ma_calc[symbol]) {
            _ma_calc[symbol] = std::make_unique<MA>(_period);
        }

        Map<String, context_t> args;
        String close_key = get_symbol(symbol) + ".close";
        if (context.exist(close_key)) {
            args["close"] = context.get<Vector<double>>(close_key);
        }

        context_t result = (*_ma_calc[symbol])(args);
        double ma = std::visit([](const auto& v) -> double {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double>) return v;
            else if constexpr (std::is_same_v<T, Vector<double>>)
                return v.empty() ? std::nan("") : v.back();
            else return std::nan("");
        }, result);

        if (std::isnan(ma)) return RiskTriggerType::None;

        if (current_price <= ma) {
            INFO("[Guard:MaStopLoss] TRIGGERED! symbol={} current={:.2f} ma={:.2f}",
                 get_symbol(symbol), current_price, ma);
            return RiskTriggerType::MaStopLoss;
        }
        return RiskTriggerType::None;
    }

    String name() const override { return "MaStopLoss"; }

    void setPeriod(int period) { _period = period; }

private:
    int _period;
    Map<symbol_t, std::unique_ptr<MA>> _ma_calc;
};

/**
 * @brief R² 趋势消失止损
 * 触发条件: R²(period) <= threshold
 * 
 * 特点：R² 衡量趋势强度，跌破阈值意味着趋势不再显著
 * R² ∈ [0, 1]，越接近 1 趋势越强
 */
class R2StopLossGuard : public Guard {
public:
    explicit R2StopLossGuard(int period = 10, double threshold = 0.5)
        : _period(period), _threshold(threshold) {
        _enabled = true;
    }

    RiskTriggerType check(symbol_t symbol, DataContext& context,
                          const EntryInfo& entry_info) override {
        if (!_enabled) return RiskTriggerType::None;

        if (!_r2_calc[symbol]) {
            _r2_calc[symbol] = std::make_unique<R2>(_period);
        }

        Map<String, context_t> args;
        String close_key = get_symbol(symbol) + ".close";
        if (context.exist(close_key)) {
            args["close"] = context.get<Vector<double>>(close_key);
        }

        context_t result = (*_r2_calc[symbol])(args);
        double r2 = std::visit([](const auto& v) -> double {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double>) return v;
            else if constexpr (std::is_same_v<T, Vector<double>>)
                return v.empty() ? std::nan("") : v.back();
            else return std::nan("");
        }, result);

        if (std::isnan(r2)) return RiskTriggerType::None;

        if (r2 <= _threshold) {
            INFO("[Guard:R2StopLoss] TRIGGERED! symbol={} r2={:.3f} threshold={:.3f}",
                 get_symbol(symbol), r2, _threshold);
            return RiskTriggerType::R2StopLoss;
        }
        return RiskTriggerType::None;
    }

    String name() const override { return "R2StopLoss"; }

    void setParams(int period, double threshold) {
        _period = period;
        _threshold = threshold;
    }

private:
    int _period;
    double _threshold;
    Map<symbol_t, std::unique_ptr<R2>> _r2_calc;
};

/**
 * @brief MAE 最大不利偏移止损（Maximum Adverse Excursion）
 * 触发条件: (entry_price - lowest_price) / entry_price >= percent
 * 
 * 与 TrailingStop 的区别：
 * - TrailingStop: 跟踪最高价回落（移动止盈）
 * - MAE: 跟踪最低价偏移（绝对亏损限制）
 * 
 * 用途：限制持仓期间的最大浮亏
 */
class MaeStopLossGuard : public Guard {
public:
    explicit MaeStopLossGuard(double percent = 0.05) : _percent(percent) {
        _enabled = true;
    }

    RiskTriggerType check(symbol_t symbol, DataContext& context,
                          const EntryInfo& entry_info) override {
        if (!_enabled || entry_info._avg_price <= 0) {
            return RiskTriggerType::None;
        }

        double lowest = entry_info._lowest_price > 0 ? entry_info._lowest_price : entry_info._avg_price;
        double mae = (entry_info._avg_price - lowest) / entry_info._avg_price;
        
        if (mae >= _percent) {
            INFO("[Guard:MaeStopLoss] TRIGGERED! symbol={} entry={:.2f} lowest={:.2f} mae={:.2f} threshold={:.2f}",
                 get_symbol(symbol), entry_info._avg_price, lowest, mae, _percent);
            return RiskTriggerType::MaeStopLoss;
        }
        return RiskTriggerType::None;
    }

    void update(symbol_t symbol, DataContext& context, EntryInfo& entry_info) override {
        // 更新最低价（用于 MAE 计算）
        double current_price = getCurrentPrice(symbol, context);
        if (current_price > 0) {
            if (entry_info._lowest_price == 0 || current_price < entry_info._lowest_price) {
                entry_info._lowest_price = current_price;
            }
        }
    }

    String name() const override { return "MaeStopLoss"; }

    void setPercent(double percent) { _percent = percent; }

private:
    double _percent;
};