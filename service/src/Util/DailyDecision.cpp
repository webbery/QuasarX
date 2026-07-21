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
        {"quantity", quantity},
        {"target_price", target_price},
        {"stop_loss", stop_loss},
        {"signal_strength", signal_strength},
        {"reason", reason},
    };
}

Decision Decision::fromJson(const nlohmann::json& j) {
    Decision d;
    d.symbol = j.value("symbol", "");
    d.action = parseAction(j.value("action", "HOLD"));
    d.quantity = j.value("quantity", 0);
    d.target_price = j.value("target_price", 0.0);
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
        {"status", status},
        {"decisions", arr},
        {"risk", risk.toJson()},
    };
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
    int buy = 0, sell = 0, hold = 0, close_cnt = 0;
    String worst_status = "completed";
    auto rank = [](const String& s) {
        if (s == "failed")  return 2;
        if (s == "partial") return 1;
        return 0;
    };

    for (auto& [name, strat] : root["strategies"].items()) {
        if (rank(strat.value("status", "completed")) > rank(worst_status))
            worst_status = strat.value("status", "completed");
        for (auto& d : strat["decisions"]) {
            auto act = parseAction(d.value("action", "HOLD"));
            if (act == Action::BUY)        ++buy;
            else if (act == Action::SELL)  ++sell;
            else if (act == Action::HOLD)  ++hold;
            else if (act == Action::CLOSE) ++close_cnt;
        }
    }

    root["status"] = worst_status;
    root["summary"] = {
        {"buy_count", buy},
        {"sell_count", sell},
        {"hold_count", hold},
        {"close_count", close_cnt},
        {"total_decisions", buy + sell + hold + close_cnt},
    };

    try {
        std::ofstream ofs(path);
        ofs << root.dump(2);
        INFO("[DailyDecision] Saved {} ({} strategies, {} decisions)",
             path, root["strategies"].size(),
             buy + sell + hold + close_cnt);
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
