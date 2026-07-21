#pragma once
#include "std_header.h"
#include "json.hpp"

/**
 * 日线策略决策 JSON 格式
 *
 * 文件路径: {data_dir}/decisions/{date}.json
 *
 * 顶层结构:
 * {
 *   "date": "2026-07-21",
 *   "generated_at": "2026-07-21T15:03:22",
 *   "status": "completed",                   // completed | partial | failed
 *   "strategies": {
 *     "trend_tracking_v13": {
 *       "executed_at": "2026-07-21T15:02:11",
 *       "status": "completed",
 *       "decisions": [
 *         { "symbol": "sz.000001", "action": "BUY",  ... },
 *         ...
 *       ],
 *       "risk": { "var_95": 0.023, "max_drawdown": -0.012, "breakers": "none" }
 *     }
 *   },
 *   "summary": { "buy_count": 3, "sell_count": 2, "hold_count": 5, "total_symbols": 10 }
 * }
 */
namespace DailyDecisionJson {

enum class Action {
    BUY,
    SELL,
    HOLD,
    CLOSE,
};

inline const char* toString(Action a) {
    switch (a) {
        case Action::BUY:   return "BUY";
        case Action::SELL:  return "SELL";
        case Action::HOLD:  return "HOLD";
        case Action::CLOSE: return "CLOSE";
    }
    return "HOLD";
}

inline Action parseAction(const String& s) {
    if (s == "BUY")   return Action::BUY;
    if (s == "SELL")  return Action::SELL;
    if (s == "CLOSE") return Action::CLOSE;
    return Action::HOLD;
}

struct Decision {
    String symbol;
    Action action = Action::HOLD;
    int quantity = 0;
    double target_price = 0.0;
    double stop_loss = 0.0;
    double signal_strength = 0.0;
    String reason;

    nlohmann::json toJson() const;
    static Decision fromJson(const nlohmann::json& j);
};

struct Risk {
    double var_95 = 0.0;
    double max_drawdown = 0.0;
    String breakers = "none";

    nlohmann::json toJson() const;
};

struct Report {
    String strategy;
    String executed_at;
    String status = "completed";
    Vector<Decision> decisions;
    Risk risk;

    nlohmann::json toJson() const;
};

// ── JSON 文件读写 ──

/**
 * 文件路径: {data_dir}/decisions/{date}.json
 * 目录不存在会自动创建
 */
String filePath(const String& data_dir, const String& date);

/**
 * 追加（或覆盖）某策略的决策报告到文件
 * 已存在则合并 strategies map
 */
bool saveReport(const String& data_dir, const String& date, const Report& report);

/**
 * 读取指定日期的完整决策文件
 * 不存在返回空 JSON: {date, strategies: {}}
 */
nlohmann::json load(const String& data_dir, const String& date);

}  // namespace DailyDecisionJson
