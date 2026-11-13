#pragma once

#include "Guard.h"
#include "BasicGuards.h"
#include "AdvancedGuards.h"
#include "json.hpp"

/**
 * @brief Guard 工厂类
 * 
 * 根据配置动态创建 Guard 实例。
 * 支持三种配置格式：
 * 1. 前端中文格式: "止损开关": { "value": true }, "止损比例": { "value": 0.05 }
 * 2. 后端英文格式: "stop_loss_enabled": { "value": true }
 * 3. 嵌套格式: "stop_loss": { "enabled": true, "percent": 0.05 }
 */
class GuardFactory {
public:
    /**
     * @brief 从配置创建 Guard 列表
     * @param params 配置参数（ProtectionNode::Init 的 params 对象）
     * @return 创建成功的 Guard 列表（unique_ptr 管理）
     */
    static Vector<std::unique_ptr<Guard>> createGuards(const nlohmann::json& params) {
        Vector<std::unique_ptr<Guard>> guards;

        // 尝试三种配置格式
        if (params.contains("止损开关")) {
            // 格式 1: 前端中文格式
            createFromChineseFormat(params, guards);
        } else if (params.contains("stop_loss_enabled")) {
            // 格式 2: 后端英文格式
            createFromEnglishFormat(params, guards);
        } else if (params.contains("stop_loss")) {
            // 格式 3: 嵌套格式
            createFromNestedFormat(params, guards);
        }

        return guards;
    }

private:
    // 格式 1: 前端中文格式
    static void createFromChineseFormat(const nlohmann::json& params,
                                        Vector<std::unique_ptr<Guard>>& guards) {
        // 止损
        if (params.value("止损开关", nlohmann::json::object())["value"].get<bool>()) {
            auto guard = std::make_unique<StopLossGuard>();
            guard->setPercent(params["止损比例"]["value"].get<double>());
            guards.push_back(std::move(guard));
        }

        // 止盈
        if (params.value("止盈开关", nlohmann::json::object())["value"].get<bool>()) {
            auto guard = std::make_unique<TakeProfitGuard>();
            guard->setPercent(params["止盈比例"]["value"].get<double>());
            guards.push_back(std::move(guard));
        }

        // 追踪止损
        if (params.value("追踪止损开关", nlohmann::json::object())["value"].get<bool>()) {
            auto guard = std::make_unique<TrailingStopGuard>();
            guard->setPercent(params["追踪止损比例"]["value"].get<double>());
            guards.push_back(std::move(guard));
        }

        // 时间止损
        if (params.value("时间止损开关", nlohmann::json::object())["value"].get<bool>()) {
            auto guard = std::make_unique<TimeStopGuard>();
            guard->setMaxBars(params["最大持仓Bar数"]["value"].get<int>());
            guards.push_back(std::move(guard));
        }

        // ATR 止损
        if (params.value("ATR止损开关", nlohmann::json::object()).value("value", false)) {
            int period = params.value("ATR周期", nlohmann::json::object({{"value", 20}}))["value"].get<int>();
            double mult = params.value("ATR倍数", nlohmann::json::object({{"value", 2.0}}))["value"].get<double>();
            guards.push_back(std::make_unique<AtrStopLossGuard>(period, mult));
        }

        // MA 止损
        if (params.value("MA止损开关", nlohmann::json::object()).value("value", false)) {
            int period = params.value("MA周期", nlohmann::json::object({{"value", 20}}))["value"].get<int>();
            guards.push_back(std::make_unique<MaStopLossGuard>(period));
        }

        // R² 止损
        if (params.value("R2止损开关", nlohmann::json::object()).value("value", false)) {
            int period = params.value("R2周期", nlohmann::json::object({{"value", 10}}))["value"].get<int>();
            double threshold = params.value("R2阈值", nlohmann::json::object({{"value", 0.5}}))["value"].get<double>();
            guards.push_back(std::make_unique<R2StopLossGuard>(period, threshold));
        }

        // MAE 止损
        if (params.value("MAE止损开关", nlohmann::json::object()).value("value", false)) {
            double percent = params.value("MAE比例", nlohmann::json::object({{"value", 0.05}}))["value"].get<double>();
            guards.push_back(std::make_unique<MaeStopLossGuard>(percent));
        }
    }

    // 格式 2: 后端英文格式
    static void createFromEnglishFormat(const nlohmann::json& params,
                                        Vector<std::unique_ptr<Guard>>& guards) {
        // 止损
        if (params.value("stop_loss_enabled", nlohmann::json::object())["value"].get<bool>()) {
            auto guard = std::make_unique<StopLossGuard>();
            guard->setPercent(params["stop_loss_percent"]["value"].get<double>());
            guards.push_back(std::move(guard));
        }

        // 止盈
        if (params.value("take_profit_enabled", nlohmann::json::object())["value"].get<bool>()) {
            auto guard = std::make_unique<TakeProfitGuard>();
            guard->setPercent(params["take_profit_percent"]["value"].get<double>());
            guards.push_back(std::move(guard));
        }

        // 追踪止损
        if (params.value("trailing_stop_enabled", nlohmann::json::object())["value"].get<bool>()) {
            auto guard = std::make_unique<TrailingStopGuard>();
            guard->setPercent(params["trailing_stop_percent"]["value"].get<double>());
            guards.push_back(std::move(guard));
        }

        // 时间止损
        if (params.value("time_stop_enabled", nlohmann::json::object())["value"].get<bool>()) {
            auto guard = std::make_unique<TimeStopGuard>();
            guard->setMaxBars(params["max_bars"]["value"].get<int>());
            guards.push_back(std::move(guard));
        }

        // ATR/MA/R2/MAE 止损（英文格式）
        if (params.value("atr_stop_loss_enabled", nlohmann::json::object()).value("value", false)) {
            int period = params.value("atr_period", nlohmann::json::object({{"value", 20}}))["value"].get<int>();
            double mult = params.value("atr_multiplier", nlohmann::json::object({{"value", 2.0}}))["value"].get<double>();
            guards.push_back(std::make_unique<AtrStopLossGuard>(period, mult));
        }

        if (params.value("ma_stop_loss_enabled", nlohmann::json::object()).value("value", false)) {
            int period = params.value("ma_period", nlohmann::json::object({{"value", 20}}))["value"].get<int>();
            guards.push_back(std::make_unique<MaStopLossGuard>(period));
        }

        if (params.value("r2_stop_loss_enabled", nlohmann::json::object()).value("value", false)) {
            int period = params.value("r2_period", nlohmann::json::object({{"value", 10}}))["value"].get<int>();
            double threshold = params.value("r2_threshold", nlohmann::json::object({{"value", 0.5}}))["value"].get<double>();
            guards.push_back(std::make_unique<R2StopLossGuard>(period, threshold));
        }

        if (params.value("mae_stop_loss_enabled", nlohmann::json::object()).value("value", false)) {
            double percent = params.value("mae_percent", nlohmann::json::object({{"value", 0.05}}))["value"].get<double>();
            guards.push_back(std::make_unique<MaeStopLossGuard>(percent));
        }
    }

    // 格式 3: 嵌套格式
    static void createFromNestedFormat(const nlohmann::json& params,
                                       Vector<std::unique_ptr<Guard>>& guards) {
        // 止损
        if (params["stop_loss"]["enabled"].get<bool>()) {
            auto guard = std::make_unique<StopLossGuard>();
            guard->setPercent(params["stop_loss"]["percent"].get<double>());
            guards.push_back(std::move(guard));
        }

        // 止盈
        if (params["take_profit"]["enabled"].get<bool>()) {
            auto guard = std::make_unique<TakeProfitGuard>();
            guard->setPercent(params["take_profit"]["percent"].get<double>());
            guards.push_back(std::move(guard));
        }

        // 追踪止损
        if (params["trailing_stop"]["enabled"].get<bool>()) {
            auto guard = std::make_unique<TrailingStopGuard>();
            guard->setPercent(params["trailing_stop"]["percent"].get<double>());
            guards.push_back(std::move(guard));
        }

        // 时间止损
        if (params["time_stop"]["enabled"].get<bool>()) {
            auto guard = std::make_unique<TimeStopGuard>();
            guard->setMaxBars(params["time_stop"]["max_bars"].get<int>());
            guards.push_back(std::move(guard));
        }

        // ATR 止损
        if (params.value("atr_stop_loss", nlohmann::json::object()).value("enabled", false)) {
            int period = params["atr_stop_loss"].value("period", 20);
            double mult = params["atr_stop_loss"].value("multiplier", 2.0);
            guards.push_back(std::make_unique<AtrStopLossGuard>(period, mult));
        }

        // MA 止损
        if (params.value("ma_stop_loss", nlohmann::json::object()).value("enabled", false)) {
            int period = params["ma_stop_loss"].value("period", 20);
            guards.push_back(std::make_unique<MaStopLossGuard>(period));
        }

        // R² 止损
        if (params.value("r2_stop_loss", nlohmann::json::object()).value("enabled", false)) {
            int period = params["r2_stop_loss"].value("period", 10);
            double threshold = params["r2_stop_loss"].value("threshold", 0.5);
            guards.push_back(std::make_unique<R2StopLossGuard>(period, threshold));
        }

        // MAE 止损
        if (params.value("mae_stop_loss", nlohmann::json::object()).value("enabled", false)) {
            double percent = params["mae_stop_loss"].value("percent", 0.05);
            guards.push_back(std::make_unique<MaeStopLossGuard>(percent));
        }
    }
};