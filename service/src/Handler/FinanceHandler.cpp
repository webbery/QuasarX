#include "Handler/FinanceHandler.h"
#include "Data/FundamentalData.h"
#include "Util/FinanceDB.h"
#include "Util/PythonRunner.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "server.h"
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════
//  任务 id
// ═══════════════════════════════════════════════════════════

String FinanceHandler::next_task_id() {
    static std::atomic<uint64_t> seq{0};
    return fmt::format("f_{}", ++seq);
}

// ═══════════════════════════════════════════════════════════
//  POST /v0/finance — 触发下载 + 导入
// ═══════════════════════════════════════════════════════════

void FinanceHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params;
    try {
        params = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid JSON"})", "application/json");
        return;
    }

    // ── 标的来源：symbols（批量，优先）→ code（单标的，兼容旧客户端）──
    auto code = params.value("code", "");
    auto symbols_param = params.value("symbols", "");

    Vector<String> codes;
    if (!symbols_param.empty()) {
        codes = split_list(symbols_param);
    } else if (!code.empty()) {
        codes.push_back(code);
    }

    if (codes.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing 'symbols' or 'code'"})", "application/json");
        return;
    }

    auto category = params.value("category", std::string("all"));
    auto start = params.value("start", std::string(""));
    auto end = params.value("end", std::string(""));
    auto env_name = params.value("env", std::string(""));

    auto db_path = _server->GetConfig().GetDatabasePath();

    // ── 类目：all 展开成六类，否则按名字精确匹配 ──
    Vector<DataCategory> categories;
    if (category == "all") {
        categories = FundamentalData::AllCategories();
    } else {
        DataCategory cat;
        if (!FundamentalData::CategoryFromKey(category, cat)) {
            res.status = 400;
            res.set_content(fmt::format(R"({{"message":"Unknown category: {}"}})", category),
                            "application/json");
            return;
        }
        categories.push_back(cat);
    }

    // 标的：外部格式 600000.SH / sh.600000 都接受
    Vector<symbol_t> sym_list;
    for (const auto& c : codes) {
        sym_list.push_back(to_symbol(toInternalSymbol(c)));
    }

    auto task_id = next_task_id();

    // 后台执行：下载 → CSV 体检（列名/季末/公告日）→ 分块入库 → DB 级复查 → 补数据
    _server->SubmitFundamentalDownload(sym_list, categories, start, end, env_name);

    nlohmann::json resp;
    resp["status"] = "started";
    resp["task_id"] = task_id;
    resp["symbol_count"] = static_cast<int>(codes.size());
    resp["category"] = category;
    if (!code.empty()) resp["code"] = code;                 // 旧客户端兼容
    if (!symbols_param.empty()) resp["symbols"] = symbols_param;
    res.set_content(resp.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  GET /v0/finance — 查询财务数据 / 列出表和标的
// ═══════════════════════════════════════════════════════════

void FinanceHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!financeDB.init(db_path + "/finance", "finance.db")) {
            res.status = 500;
            res.set_content(R"({"message":"FinanceDB not initialized"})", "application/json");
            return;
        }
    }

    auto category = req.get_param_value("category");
    auto code = req.get_param_value("code");
    auto start = req.get_param_value("start");
    auto end = req.get_param_value("end");
    auto limit_str = req.get_param_value("limit");
    int limit = limit_str.empty() ? 500 : std::stoi(limit_str);

    // 无 category → 列出所有表和标的
    if (category.empty()) {
        nlohmann::json resp;
        auto tables = financeDB.listTables();
        nlohmann::json tables_json = nlohmann::json::array();

        for (const auto& table : tables) {
            if (!FinanceDB::isValidCategory(table)) continue;
            nlohmann::json t;
            t["category"] = table;
            t["name"] = FinanceDB::categoryName(table);
            t["symbols"] = financeDB.listSymbols(table);
            tables_json.push_back(t);
        }

        resp["tables"] = tables_json;
        res.set_content(resp.dump(), "application/json");
        return;
    }

    // 有 category 无 code → 列出该表标的
    if (code.empty()) {
        if (!FinanceDB::isValidCategory(category)) {
            res.status = 400;
            res.set_content(fmt::format(R"({{"message":"Unknown category: {}"}})", category),
                            "application/json");
            return;
        }
        nlohmann::json resp;
        resp["category"] = category;
        resp["name"] = FinanceDB::categoryName(category);
        resp["symbols"] = financeDB.listSymbols(category);
        res.set_content(resp.dump(), "application/json");
        return;
    }

    // 完整查询
    auto result = financeDB.query(category, code, start, end, limit);
    res.set_content(result.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  DELETE /v0/finance — 删除指定标的的财务数据
// ═══════════════════════════════════════════════════════════

void FinanceHandler::del(const httplib::Request& req, httplib::Response& res) {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        res.status = 500;
        res.set_content(R"({"message":"FinanceDB not initialized"})", "application/json");
        return;
    }

    auto category = req.get_param_value("category");
    auto code = req.get_param_value("code");

    if (code.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing 'code' parameter"})", "application/json");
        return;
    }

    nlohmann::json resp;
    resp["code"] = code;
    if (!category.empty()) resp["category"] = category;

    if (category.empty()) {
        // 删除所有表中该标的数据
        auto tables = financeDB.listTables();
        int deleted_tables = 0;
        for (const auto& t : tables) {
            if (FinanceDB::isValidCategory(t) && financeDB.deleteSymbol(t, code))
                deleted_tables++;
        }
        resp["deleted_tables"] = deleted_tables;
    } else {
        bool ok = financeDB.deleteSymbol(category, code);
        resp["success"] = ok;
    }
    res.set_content(resp.dump(), "application/json");
}
