#include "Handler/FinanceHandler.h"
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
    auto finance_dir = db_path + "/finance";
    auto csv_dir = finance_dir + "/csv";

    // 初始化 FinanceDB
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        if (!financeDB.init(finance_dir, "finance.db")) {
            res.status = 500;
            res.set_content(R"({"message":"Failed to init FinanceDB"})", "application/json");
            return;
        }
    }

    if (category != "all" && !FinanceDB::isValidCategory(category)) {
        res.status = 400;
        res.set_content(fmt::format(R"({{"message":"Unknown category: {}"}})", category),
                        "application/json");
        return;
    }

    nng_socket sse_sock = _server->GetSocket();
    auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
    auto interpreter = pyEnv.resolve(env_name);
    auto task_id = next_task_id();

    // 后台线程：逐标的下载 + 就地导入（结构同 QuoteDownloadHandler::runDownloadJob）
    std::thread([sse_sock, codes, category, start, end, interpreter, csv_dir, task_id]() {
        SetCurrentThreadName("FinanceDownload");

        // 确定要导入的类别
        std::vector<std::string> categories;
        if (category == "all") {
            categories = {"profit", "operation", "growth", "balance", "cashflow", "dupont"};
        } else {
            categories = {category};
        }

        auto& financeDB = FinanceDB::instance();
        auto total = std::to_string(codes.size());

        SendSSE(sse_sock, "finance_download", {
            {"status", "started"}, {"task_id", task_id}, {"total", total}
        });

        int total_rows = 0;
        int processed = 0;
        int failed = 0;

        for (const auto& sym : codes) {
            std::string cmd = interpreter + " tools/fetch_finance_data.py"
                            + " --code " + sym
                            + " --download"
                            + " --data-dir " + csv_dir;
            if (category != "all") cmd += " --category " + category;
            if (!start.empty()) cmd += " --start " + start;
            if (!end.empty()) cmd += " --end " + end;

            String output;
            bool ok = RunCommand(cmd, output);
            processed++;

            if (!ok || !financeDB.isInitialized()) {
                failed++;
                WARN("[FinanceHandler] {} download failed: {}", sym, output);
                SendSSE(sse_sock, "finance_download", {
                    {"status", "symbol_failed"}, {"task_id", task_id},
                    {"code", sym}, {"error", "download failed"},
                    {"downloaded", std::to_string(processed)}, {"total", total}
                });
                continue;
            }

            // 就地导入该标的的 CSV：按标的精确定位，不扫全目录，
            // 避免并发任务互相吞掉对方还没导入的 CSV
            int rows = 0;
            for (const auto& cat : categories) {
                auto path = csv_dir + "/" + cat + "/" + sym + ".csv";
                if (!fs::exists(path)) continue;
                int n = financeDB.importCsv(path, cat);
                if (n > 0) rows += n;
                fs::remove(path);
            }
            total_rows += rows;

            INFO("[FinanceHandler] {} done: {} rows", sym, rows);
            SendSSE(sse_sock, "finance_download", {
                {"status", "symbol_downloaded"}, {"task_id", task_id},
                {"code", sym}, {"rows", std::to_string(rows)},
                {"downloaded", std::to_string(processed)}, {"total", total}
            });
        }

        SendSSE(sse_sock, "finance_download", {
            {"status", "done"}, {"task_id", task_id},
            {"total_rows", std::to_string(total_rows)},
            {"failed", std::to_string(failed)},
            {"success", failed == 0 ? "true" : "false"}
        });
    }).detach();

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
