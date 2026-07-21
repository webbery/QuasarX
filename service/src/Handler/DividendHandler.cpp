#include "Handler/DividendHandler.h"
#include "Util/FinanceDB.h"
#include "Util/PythonRunner.h"
#include "Util/system.h"
#include "server.h"
#include <filesystem>
#include <thread>
#include <set>
#include <boost/algorithm/string/join.hpp>

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════
//  POST /v0/dividend — 手动触发下载 + 导入
// ═══════════════════════════════════════════════════════════

void DividendHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params;
    try {
        params = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid JSON"})", "application/json");
        return;
    }

    // ── action=recalc: 触发后复权价格重算 ──
    auto action = params.value("action", std::string(""));
    if (action == "recalc") {
        auto& financeDB = FinanceDB::instance();
        if (!financeDB.isInitialized()) {
            auto db_path = _server->GetConfig().GetDatabasePath();
            if (!financeDB.init(db_path + "/finance")) {
                res.status = 500;
                res.set_content(R"({"message":"FinanceDB init failed"})", "application/json");
                return;
            }
        }

        auto code = params.value("code", std::string(""));
        nlohmann::json resp;
        if (code.empty()) {
            // 重算所有标的
            resp = financeDB.recalcAllAdjPrices();
        } else {
            // 重算单个标的
            int n = financeDB.recalcSymbolAdjPrices(code);
            resp["code"] = code;
            resp["bars"] = n;
            resp["status"] = n >= 0 ? "completed" : "error";
        }
        res.set_content(resp.dump(), "application/json");
        return;
    }

    // ── action=import: 导入本地 CSV 到 dividend 表 ──
    if (action == "import") {
        auto& financeDB = FinanceDB::instance();
        if (!financeDB.isInitialized()) {
            auto db_path = _server->GetConfig().GetDatabasePath();
            if (!financeDB.init(db_path + "/finance")) {
                res.status = 500;
                res.set_content(R"({"message":"FinanceDB init failed"})", "application/json");
                return;
            }
        }
        auto dividend_dir = params.value("dividend_dir", std::string("data/dividend"));
        int total = financeDB.importAllDividends(dividend_dir);
        nlohmann::json resp;
        resp["status"] = "completed";
        resp["imported_rows"] = total;
        res.set_content(resp.dump(), "application/json");
        return;
    }

    // ── 默认: 触发下载 + 导入 ──
    String symbols;
    if (params.contains("symbols") && !params["symbols"].get<String>().empty()) {
        symbols = params["symbols"].get<String>();
    } else if (params.value("from_strategies", true)) {
        // 从活跃策略收集标的
        auto* strategySystem = _server->GetStrategySystem();
        if (strategySystem) {
            std::set<String> allSymbols;
            auto names = strategySystem->GetStrategyNames();
            for (auto& name : names) {
                auto pools = strategySystem->GetPools(name);
                for (auto sym : pools) {
                    allSymbols.insert(get_symbol(sym));
                }
            }
            symbols = boost::algorithm::join(
                Vector<String>(allSymbols.begin(), allSymbols.end()), ",");
        }
    }
    if (symbols.empty()) {
        symbols = "000001.SZ,000858.SZ,600036.SH,600519.SH,601318.SH";
        WARN("[DividendHandler] No active strategies, using default symbols");
    }

    auto years = params.value("years", std::string(""));
    auto env_name = params.value("env", std::string(""));

    auto db_path = _server->GetConfig().GetDatabasePath();
    auto data_dir = db_path;

    nng_socket sse_sock = _server->GetSocket();
    auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
    auto interpreter = pyEnv.resolve(env_name);

    // 后台线程：下载 + 导入
    std::thread([sse_sock, symbols, years, interpreter, data_dir, db_path]() {
        SetCurrentThreadName("DividendDownload");

        std::string cmd = interpreter + " tools/fetch_dividend_data.py"
                        + " \"" + symbols + "\""
                        + " --download"
                        + " --data-dir " + data_dir;
        if (!years.empty()) cmd += " --years " + years;

        SendSSE(sse_sock, "dividend_download", {
            {"status", "started"},
            {"symbol_count", std::to_string(std::count(symbols.begin(), symbols.end(), ',') + 1)}
        });

        String output;
        bool ok = RunCommand(cmd, output);

        SendSSE(sse_sock, "dividend_download", {
            {"status", ok ? "downloaded" : "download_failed"},
            {"output", output}
        });

        // 初始化 FinanceDB
        auto& financeDB = FinanceDB::instance();
        if (!financeDB.isInitialized()) {
            if (!financeDB.init(db_path + "/finance")) {
                SendSSE(sse_sock, "dividend_download", {
                    {"status", "aborted"},
                    {"reason", "FinanceDB init failed"}
                });
                return;
            }
        }

        // 导入 CSV 到 dividend 表
        int total = financeDB.importAllDividends(data_dir + "/dividend");

        SendSSE(sse_sock, "dividend_download", {
            {"status", "completed"},
            {"imported_rows", std::to_string(total)},
            {"success", ok ? "true" : "false"}
        });
    }).detach();

    nlohmann::json resp;
    resp["status"] = "started";
    resp["symbol_count"] = static_cast<int>(std::count(symbols.begin(), symbols.end(), ',')) + 1;
    res.set_content(resp.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  GET /v0/dividend — 查询分红数据
//
//  参数:
//    date=YYYY-MM-DD  → 查询该日所有除权除息事件
//    code=XXXXXX.SH   → 查询该标的分红历史（可选 start/end）
//    start=YYYY-MM-DD, end=YYYY-MM-DD
//    无参数           → 列出所有已导入分红数据的标的
// ═══════════════════════════════════════════════════════════

void DividendHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!financeDB.init(db_path + "/finance")) {
            res.status = 500;
            res.set_content(R"({"message":"FinanceDB not initialized"})", "application/json");
            return;
        }
    }

    auto date = req.get_param_value("date");
    auto code = req.get_param_value("code");
    auto start = req.get_param_value("start");
    auto end = req.get_param_value("end");

    // 按日期查询
    if (!date.empty()) {
        auto result = financeDB.queryDividendByDate(date);
        res.set_content(result.dump(), "application/json");
        return;
    }

    // 按标的查询
    if (!code.empty()) {
        auto result = financeDB.queryDividendBySymbol(code, start, end);
        res.set_content(result.dump(), "application/json");
        return;
    }

    // 无参数：列出所有有分红数据的标的
    nlohmann::json resp;
    auto symbols = financeDB.listSymbols("dividend");
    resp["count"] = static_cast<int>(symbols.size());
    resp["symbols"] = symbols;
    res.set_content(resp.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  DELETE /v0/dividend — 删除指定标的的分红数据
// ═══════════════════════════════════════════════════════════

void DividendHandler::del(const httplib::Request& req, httplib::Response& res) {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!financeDB.init(db_path + "/finance")) {
            res.status = 500;
            res.set_content(R"({"message":"FinanceDB not initialized"})", "application/json");
            return;
        }
    }

    auto code = req.get_param_value("code");
    if (code.empty()) {
        // 删除整张表
        if (financeDB.dropTable("dividend")) {
            nlohmann::json resp;
            resp["status"] = "dropped";
            res.set_content(resp.dump(), "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"message":"Failed to drop table"})", "application/json");
        }
        return;
    }

    if (financeDB.deleteSymbol("dividend", code)) {
        nlohmann::json resp;
        resp["status"] = "deleted";
        resp["code"] = code;
        res.set_content(resp.dump(), "application/json");
    } else {
        res.status = 500;
        res.set_content(R"({"message":"Failed to delete"})", "application/json");
    }
}
