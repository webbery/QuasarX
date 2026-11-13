#include "Handler/DividendHandler.h"
#include "Util/FinanceDB.h"
#include "Util/PythonRunner.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "server.h"
#include <filesystem>
#include <thread>
#include <set>
#include <sstream>
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
            if (!financeDB.init(db_path + "/finance", "finance.db")) {
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
            auto events = financeDB.getDividendEvents(code);
            int n = financeDB.recalcSymbolAdjPrices(code, events);
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
            if (!financeDB.init(db_path + "/finance", "finance.db")) {
                res.status = 500;
                res.set_content(R"({"message":"FinanceDB init failed"})", "application/json");
                return;
            }
        }
        auto dividend_dir = params.value("dividend_dir", std::string("data/dividend"));

        // 可选作用域：给了 symbols 就只导入这些标的的 CSV。
        // 目录里可能躺着别的任务/别的测试留下的 CSV，全量导入会把池外标的的
        // 分红事件写进 dividend 表；反过来，某个文件失败也不该把整次导入
        // 已成功的行数吃掉 —— 两者分开统计。
        Set<String> scope;
        if (params.contains("symbols") && !params["symbols"].get<String>().empty()) {
            Vector<String> tokens;
            split(params["symbols"].get<String>(), tokens, ",");
            for (const auto& t : tokens) {
                if (!t.empty()) scope.insert(toInternalSymbol(t));
            }
        }

        int failed_files = 0;
        int total = financeDB.importAllDividends(dividend_dir, scope, &failed_files);
        nlohmann::json resp;
        resp["status"] = "completed";
        resp["imported_rows"] = total;
        resp["failed_files"] = failed_files;
        res.set_content(resp.dump(), "application/json");
        return;
    }

    // ── 默认: 触发下载 + 导入 ──
    String symbols;
    if (params.contains("symbols") && !params["symbols"].get<String>().empty()) {
        symbols = params["symbols"].get<String>();
    } else if (params.contains("strategy_name") && !params["strategy_name"].get<String>().empty()) {
        // 从指定策略收集标的
        auto* strategySystem = _server->GetStrategySystem();
        if (strategySystem) {
            auto name = params["strategy_name"].get<String>();
            auto pools = strategySystem->GetPools(name);
            std::set<String> symSet;
            for (auto sym : pools) {
                symSet.insert(get_symbol(sym));
            }
            symbols = boost::algorithm::join(
                Vector<String>(symSet.begin(), symSet.end()), ",");
        }
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

    auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
    auto interpreter = pyEnv.resolve(env_name);

    // 后台线程：下载 + 导入
    std::thread([symbols, years, interpreter, data_dir, db_path]() {
        SetCurrentThreadName("DividendDownload");
        // SSE socket 必须在当前工作线程创建并关闭
        ScopedSocket sse(Server::GetSocket());

        std::string cmd = interpreter + " tools/fetch_dividend_data.py"
                        + " \"" + symbols + "\""
                        + " --download"
                        + " --data-dir " + data_dir;
        if (!years.empty()) cmd += " --years " + years;

        INFO("[DividendHandler] Running: {}", cmd);

        SendSSE(sse.get(), "dividend_download", {
            {"status", "started"},
            {"symbol_count", std::to_string(std::count(symbols.begin(), symbols.end(), ',') + 1)}
        });

        String output;
        bool ok = RunCommand(cmd, output);

        INFO("[DividendHandler] RunCommand returned ok={}, output={}", ok, output);

        SendSSE(sse.get(), "dividend_download", {
            {"status", ok ? "downloaded" : "download_failed"},
            {"output", output}
        });

        // 初始化 FinanceDB
        auto& financeDB = FinanceDB::instance();
        if (!financeDB.isInitialized()) {
            if (!financeDB.init(db_path + "/finance", "finance.db")) {
                SendSSE(sse.get(), "dividend_download", {
                    {"status", "aborted"},
                    {"reason", "FinanceDB init failed"}
                });
                return;
            }
        }

        // 导入 CSV 到 dividend 表。
        // 只导入本次请求的标的：目录里可能躺着别的任务留下的 CSV，
        // 导入池外标的的分红事件会让后续复权重算按错误事件算因子。
        Set<String> scope;
        {
            Vector<String> tokens;
            split(symbols, tokens, ",");
            for (const auto& t : tokens) {
                if (!t.empty()) scope.insert(toInternalSymbol(t));
            }
        }
        int total = 0;
        int failed_files = 0;
        if (!ok) {
            // 下载失败不导入：残留 CSV 是旧数据，导入后会让复权因子错乱
            WARN("[DividendHandler] 下载失败，跳过分红导入");
        } else {
            total = financeDB.importAllDividends(data_dir + "/dividend", scope, &failed_files);
            if (failed_files > 0) {
                WARN("[DividendHandler] 分红导入有 {} 个文件失败，事件表不完整", failed_files);
            }
        }

        SendSSE(sse.get(), "dividend_download", {
            {"status", "completed"},
            {"imported_rows", std::to_string(total)},
            {"failed_files", std::to_string(failed_files)},
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
        if (!financeDB.init(db_path + "/finance", "finance.db")) {
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

    // 无参数：返回全部记录（前端分页）
    auto result = financeDB.queryAllDividends(10000, 0);
    res.set_content(result.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  DELETE /v0/dividend — 删除指定标的的分红数据
// ═══════════════════════════════════════════════════════════

void DividendHandler::del(const httplib::Request& req, httplib::Response& res) {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!financeDB.init(db_path + "/finance", "finance.db")) {
            res.status = 500;
            res.set_content(R"({"message":"FinanceDB not initialized"})", "application/json");
            return;
        }
    }

    auto code = req.get_param_value("code");
    auto ex_date = req.get_param_value("ex_date");

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

    if (!ex_date.empty()) {
        // 删除单次分红事件（按 symbol + ex_dividend_date 唯一定位）
        if (financeDB.deleteDividendEvent(code, ex_date)) {
            nlohmann::json resp;
            resp["status"] = "deleted";
            resp["code"] = code;
            resp["ex_date"] = ex_date;
            res.set_content(resp.dump(), "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"message":"Failed to delete dividend event"})", "application/json");
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
