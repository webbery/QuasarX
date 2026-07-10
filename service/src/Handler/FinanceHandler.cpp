#include "Handler/FinanceHandler.h"
#include "Util/FinanceDB.h"
#include "Util/PythonRunner.h"
#include "Util/system.h"
#include "server.h"
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

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

    auto code = params.value("code", "");
    if (code.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing 'code'"})", "application/json");
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
        if (!financeDB.init(finance_dir)) {
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

    // 后台线程：下载 + 导入
    std::thread([sse_sock, code, category, start, end, interpreter, csv_dir, finance_dir]() {
        SetCurrentThreadName("FinanceDownload");

        std::string cmd = interpreter + " tools/fetch_finance_data.py"
                        + " --code " + code
                        + " --download"
                        + " --data-dir " + csv_dir;
        if (category != "all") cmd += " --category " + category;
        if (!start.empty()) cmd += " --start " + start;
        if (!end.empty()) cmd += " --end " + end;

        SendSSE(sse_sock, "finance_download", {
            {"status", "started"}, {"code", code}, {"category", category}
        });

        String output;
        bool ok = RunCommand(cmd, output);

        SendSSE(sse_sock, "finance_download", {
            {"status", ok ? "downloaded" : "download_failed"},
            {"output", output}
        });

        auto& financeDB = FinanceDB::instance();
        if (!ok || !financeDB.isInitialized()) {
            SendSSE(sse_sock, "finance_download", {
                {"status", "aborted"},
                {"reason", ok ? "FinanceDB shutting down" : "download failed"}
            });
            return;
        }

        // 确定要导入的类别
        std::vector<std::string> categories;
        if (category == "all") {
            categories = {"profit", "operation", "growth", "balance", "cashflow", "dupont"};
        } else {
            categories = {category};
        }

        // 扫描 CSV 并导入 DuckDB
        int total_rows = 0;
        for (const auto& cat : categories) {
            std::string cat_dir = csv_dir + "/" + cat;
            if (!fs::exists(cat_dir)) continue;

            for (auto& entry : fs::directory_iterator(cat_dir)) {
                if (entry.path().extension() != ".csv") continue;

                int rows = financeDB.importCsv(entry.path().string(), cat);
                if (rows > 0) {
                    total_rows += rows;
                    SendSSE(sse_sock, "finance_download", {
                        {"status", "importing"},
                        {"category", cat},
                        {"file", entry.path().filename().string()},
                        {"rows", std::to_string(rows)}
                    });
                }
                fs::remove(entry.path());
            }
        }

        SendSSE(sse_sock, "finance_download", {
            {"status", "done"},
            {"code", code},
            {"category", category},
            {"total_rows", std::to_string(total_rows)},
            {"success", ok ? "true" : "false"}
        });
    }).detach();

    nlohmann::json resp;
    resp["status"] = "started";
    resp["code"] = code;
    resp["category"] = category;
    res.set_content(resp.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  GET /v0/finance — 查询财务数据 / 列出表和标的
// ═══════════════════════════════════════════════════════════

void FinanceHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!financeDB.init(db_path + "/finance")) {
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
