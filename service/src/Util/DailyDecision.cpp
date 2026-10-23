#include "Util/DailyDecision.h"
#include "Util/log.h"
#include "Util/datetime.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace DailyDecisionJson;

// ═══════════════════════════════════════════════════════════
//  序列化
// ═══════════════════════════════════════════════════════════

nlohmann::json Decision::toJson() const {
    return {
        {"symbol", symbol},
        {"action", toString(action)},
        {"flag", static_cast<int>(flag)},
        {"quantity", quantity},
        {"target_price", target_price},
        {"close_price", close_price},
        {"stop_loss", stop_loss},
        {"signal_strength", signal_strength},
        {"reason", reason},
    };
}

Decision Decision::fromJson(const nlohmann::json& j) {
    Decision d;
    d.symbol = j.value("symbol", "");
    d.action = parseTradeAction(j.value("action", "HOLD"));
    d.flag = static_cast<unsigned char>(j.value("flag", 0));
    d.quantity = j.value("quantity", 0);
    d.target_price = j.value("target_price", 0.0);
    d.close_price = j.value("close_price", 0.0);
    d.stop_loss = j.value("stop_loss", 0.0);
    d.signal_strength = j.value("signal_strength", 0.0);
    d.reason = j.value("reason", "");
    return d;
}

nlohmann::json Risk::toJson() const {
    return {
        {"var_95", var_95},
        {"max_drawdown", max_drawdown},
        {"breakers", breakers},
    };
}

nlohmann::json Report::toJson() const {
    nlohmann::json::array_t arr;
    for (auto& d : decisions) arr.push_back(d.toJson());
    return {
        {"executed_at", executed_at},
        {"status", to_string(status)},
        {"decisions", arr},
        {"risk", risk.toJson()},
    };
}

Report Report::FromDecisionSet(const DecisionSet& set) {
    Report r;
    r.strategy = set._strategy;
    r.executed_at = set._generatedAt;
    r.status = set._status;

    // HOLD 也进 decisions 数组：它是一档真实存在的决策（"评估过、不动手"），
    // 此前被过滤掉，导致落盘的 hold_count 恒为 0，邮件与日报计数对不上。
    r.decisions.reserve(set._items.size());
    for (const auto& it : set._items) {
        Decision d;
        d.symbol = get_symbol(it._symbol);
        d.action = it._action;
        d.flag = it._flag;
        d.quantity = static_cast<int>(it._quantity);
        d.target_price = it._price;
        d.close_price = it._closePrice;
        r.decisions.push_back(std::move(d));
    }
    return r;
}

// ═══════════════════════════════════════════════════════════
//  文件路径
// ═══════════════════════════════════════════════════════════

String DailyDecisionJson::filePath(const String& data_dir, const String& date) {
    return data_dir + "/decisions/" + date + ".json";
}

// ═══════════════════════════════════════════════════════════
//  保存决策报告
//
//  合并逻辑:
//  - 文件不存在 → 新建，包含完整顶层结构
//  - 文件存在 → 读取已有 strategies，合并当前 report.strategy 的结果
//  - generated_at 更新为本次保存时间
//  - status 取所有策略 status 中最低的（completed > partial > failed）
//  - summary 重新计算
// ═══════════════════════════════════════════════════════════

bool DailyDecisionJson::saveReport(const String& data_dir,
                                    const String& date,
                                    const Report& report) {
    String path = filePath(data_dir, date);
    fs::create_directories(fs::path(path).parent_path());

    nlohmann::json root;
    if (fs::exists(path)) {
        try {
            std::ifstream ifs(path);
            root = nlohmann::json::parse(ifs);
        } catch (...) {
            WARN("[DailyDecision] Failed to parse existing {}, overwriting", path);
            root = nlohmann::json::object();
        }
    }

    if (!root.contains("strategies")) root["strategies"] = nlohmann::json::object();
    root["date"] = date;
    root["generated_at"] = ToString(Now(), "%Y-%m-%d %H:%M:%S");

    // 合并/覆盖当前策略的报告
    root["strategies"][report.strategy] = report.toJson();

    // 重新计算 summary + 整体 status
    //
    // status 词汇与排序统一用 DecisionSet 的定义：此前这里写死
    // "failed" > "partial" > 其他，而生产方只发 "error"/"completed"，
    // 于是 "error" 落到兜底分支得 0 分，顶层 status 永远停在 "completed"，
    // 策略报错在日报上看不出来。
    int buy = 0, sell = 0, hold = 0, other = 0;
    DecisionStatus worst_status = DecisionStatus::Completed;

    for (auto& [name, strat] : root["strategies"].items()) {
        auto st = parse_decision_status(strat.value("status", "completed"));
        if (decision_status_rank(st) > decision_status_rank(worst_status)) {
            worst_status = st;
        }
        for (auto& d : strat["decisions"]) {
            auto act = parseTradeAction(d.value("action", "HOLD"));
            if (act == TradeAction::BUY)       ++buy;
            else if (act == TradeAction::SELL) ++sell;
            else if (act == TradeAction::HOLD) ++hold;
            else                               ++other;   // EXEC 等
        }
    }

    root["status"] = to_string(worst_status);
    root["summary"] = {
        {"buy_count", buy},
        {"sell_count", sell},
        {"hold_count", hold},
        {"other_count", other},
        {"total_decisions", buy + sell + hold + other},
    };

    try {
        std::ofstream ofs(path);
        ofs << root.dump(2);
        INFO("[DailyDecision] Saved {} ({} strategies, {} decisions)",
             path, root["strategies"].size(),
             buy + sell + hold + other);
        return true;
    } catch (...) {
        WARN("[DailyDecision] Failed to write {}", path);
        return false;
    }
}

// ═══════════════════════════════════════════════════════════
//  读取
// ═══════════════════════════════════════════════════════════

nlohmann::json DailyDecisionJson::load(const String& data_dir, const String& date) {
    String path = filePath(data_dir, date);
    if (!fs::exists(path)) {
        return nlohmann::json{
            {"date", date},
            {"status", "empty"},
            {"strategies", nlohmann::json::object()},
        };
    }
    try {
        std::ifstream ifs(path);
        return nlohmann::json::parse(ifs);
    } catch (...) {
        WARN("[DailyDecision] Failed to parse {}", path);
        return nlohmann::json{
            {"date", date},
            {"status", "parse_error"},
            {"strategies", nlohmann::json::object()},
        };
    }
}
