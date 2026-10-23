#pragma once
#include "std_header.h"
#include "json.hpp"
#include "Util/DecisionSet.h"

/**
 * 日线策略决策 JSON 格式
 *
 * 文件路径: {data_dir}/decisions/{date}.json
 *
 * 顶层结构:
 * {
 *   "date": "2026-07-21",
 *   "generated_at": "2026-07-21T15:03:22",
 *   "status": "completed",                   // completed | partial | error
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

/**
 * @brief 单条决策（落盘形态）
 *
 * 动作词用 TradeAction（与 ExecuteNode 产出的 signal 同源），
 * 不再另立一套 Action 枚举 —— 两套同义枚举正是"动作词对不上"的根源。
 * 开/平仓方向由 _flag 承载（0=开仓, 1=平仓）。
 */
struct Decision {
    String symbol;
    TradeAction action = TradeAction::HOLD;
    unsigned char flag = 0;      // 0=开仓, 1=平仓
    int quantity = 0;
    double target_price = 0.0;   // 决策价
    double close_price = 0.0;    // 当期真实收盘价（持仓估值口径）
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
    DecisionStatus status = DecisionStatus::Completed;
    Vector<Decision> decisions;
    Risk risk;

    nlohmann::json toJson() const;

    // 从 DecisionSet 生成：status、动作词、HOLD 档都取自同一份产物，
    // 不再由调用方各自拼装（各自拼装正是 hold_count 与 status 走样的来源）。
    static Report FromDecisionSet(const DecisionSet& set);
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
