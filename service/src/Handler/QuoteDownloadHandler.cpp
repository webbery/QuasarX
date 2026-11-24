#include "Handler/QuoteDownloadHandler.h"
#include "Util/QuoteDB.h"
#include "Util/FinanceDB.h"
#include "Util/PythonRunner.h"
#include "Util/system.h"
#include "Util/data.h"
#include "Util/string_algorithm.h"
#include "Util/ThreadPool.h"
#include "server.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <chrono>

void QuoteDownloadHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params;
    try {
        params = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid JSON"})", "application/json");
        return;
    }

    auto symbols = params.value("symbols", "");
    if (symbols.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing 'symbols'"})", "application/json");
        return;
    }

    auto freq = params.value("freq", "5m");
    auto env_name = params.value("env", std::string(""));
    bool overwrite = params.value("overwrite", false);
    auto db_path = _server->GetConfig().GetDatabasePath();
    auto quote_dir = db_path + "/quote";

    // 初始化 QuoteDB（下载/导入都在数据线程池里跑，这里只保证库可用）
    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        if (!quoteDB.init(quote_dir, "quote.db")) {
            res.status = 500;
            res.set_content(R"({"message":"Failed to init QuoteDB"})", "application/json");
            return;
        }
    }

    // CSV 格式的标的列表 → symbol_t
    auto sym_list = split_list(symbols);
    Vector<symbol_t> parsed;
    std::vector<std::string> etf_syms, stock_syms;
    for (auto& s : sym_list) {
        std::string bs_sym = toInternalSymbol(s);
        symbol_t sym = to_symbol(bs_sym);
        parsed.push_back(sym);
        if (is_etf(sym)) etf_syms.push_back(bs_sym);
        else             stock_syms.push_back(bs_sym);
    }

    if (parsed.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"no valid symbols"})", "application/json");
        return;
    }

    // 异步执行：下载 → CSV 体检 → 分块入库 → DB 级复查 → 补数据
    // job_id 回给调用方，前端用它把后续 SSE 进度事件关联到同一条任务
    const DataJobDispatch dispatch =
        _server->SubmitQuoteDownload(parsed, freq, overwrite, env_name);

    nlohmann::json resp;
    resp["status"] = "started";
    resp["job_id"] = dispatch._jobId;
    // 复用了进行中的任务：本次没真的派发，前端别再等一个不会来的新任务
    resp["reused"] = !dispatch._dispatched;
    resp["skipped_symbols"] = nlohmann::json::array();
    for (const auto& sym : dispatch._skipped) resp["skipped_symbols"].push_back(get_symbol(sym));
    resp["downloading_symbols"] = nlohmann::json::array();
    for (const auto& sym : dispatch._dispatching) {
        resp["downloading_symbols"].push_back(get_symbol(sym));
    }
    resp["freq"] = freq;
    resp["table_stock"] = QuoteDB::tableName("stock", freq);
    resp["table_etf"] = QuoteDB::tableName("etf", freq);
    resp["symbols"] = symbols;
    resp["stock_count"] = stock_syms.size();
    resp["etf_count"] = etf_syms.size();
    res.set_content(resp.dump(), "application/json");
}

void QuoteDownloadHandler::ShutdownDownloads() {
    ThreadPool::instance().shutdown();
}

void QuoteDownloadHandler::get(const httplib::Request& req, httplib::Response& res) {
    // 查任务状态：SSE 推送漏了（客户端没连上、后端重启）时，
    // 前端靠这个接口把「服务端已经导完、前端卡片还在转圈」补回来
    const String jobIdParam = req.get_param_value("job_id");
    if (!jobIdParam.empty()) {
        const JobId jobId = std::strtoull(jobIdParam.c_str(), nullptr, 10);
        auto session = _server->GetJobSessions().Find(jobId);
        if (!session) {
            res.status = 404;
            res.set_content(R"({"error":"unknown job_id"})", "application/json");
            return;
        }
        nlohmann::json snap = session->snapshot(true);
        nlohmann::json out = nlohmann::json::object();
        out["job_id"] = jobId;
        for (auto& [k, v] : snap.items()) {
            if (k == "meta") {
                for (auto& [mk, mv] : v.items()) out[mk] = mv;
            } else {
                out[k] = v;
            }
        }
        res.set_content(out.dump(), "application/json");
        return;
    }

    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!quoteDB.init(db_path + "/quote", "quote.db")) {
            res.status = 500;
            res.set_content(R"({"message":"QuoteDB not initialized"})", "application/json");
            return;
        }
    }

    // 如果传入了 table 和 symbol，查询具体数据
    auto table = req.get_param_value("table");
    auto symbol = req.get_param_value("symbol");
    if (!table.empty() && !symbol.empty()) {
        auto start = req.get_param_value("start");
        auto end = req.get_param_value("end");
        auto limit_str = req.get_param_value("limit");
        int limit = limit_str.empty() ? 5000 : std::stoi(limit_str);

        auto bars = quoteDB.query(table, symbol, start, end, limit);

        nlohmann::json result;
        result["table"] = table;
        result["symbol"] = symbol;
        result["count"] = bars.size();
        result["data"] = nlohmann::json::array();

        for (auto& bar : bars) {
            nlohmann::json bar_json = {
                {"datetime", bar.datetime},
                {"open", bar.open},
                {"close", bar.close},
                {"high", bar.high},
                {"low", bar.low},
                {"volume", bar.volume},
                {"turnover", bar.turnover},
                {"suspended", (bar.ext & 0x01) != 0}
            };
            // 仅当后复权数据存在时返回
            if (bar.adj_open != 0) bar_json["adj_open"] = bar.adj_open;
            if (bar.adj_close != 0) bar_json["adj_close"] = bar.adj_close;
            if (bar.adj_high != 0) bar_json["adj_high"] = bar.adj_high;
            if (bar.adj_low != 0) bar_json["adj_low"] = bar.adj_low;
            result["data"].push_back(bar_json);
        }

        res.set_content(result.dump(), "application/json");
        return;
    }

    // 否则列出所有表和标的（带时间范围）
    auto tables = quoteDB.listTables();
    nlohmann::json result = nlohmann::json::array();

    for (const auto& tbl : tables) {
        auto ranges = quoteDB.getSymbolTimeRanges(tbl);
        nlohmann::json table_info;
        table_info["table"] = tbl;
        table_info["symbol_count"] = ranges.size();
        table_info["symbols"] = nlohmann::json::array();

        for (const auto& range : ranges) {
            nlohmann::json sym_info;
            sym_info["symbol"] = get_symbol(range.symbol);
            sym_info["start_time"] = range.start_time;
            sym_info["end_time"] = range.end_time;
            sym_info["count"] = range.count;
            table_info["symbols"].push_back(sym_info);
        }

        result.push_back(table_info);
    }

    res.set_content(result.dump(), "application/json");
}

void QuoteDownloadHandler::del(const httplib::Request& req, httplib::Response& res) {
    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!quoteDB.init(db_path + "/quote", "quote.db")) {
            res.status = 500;
            res.set_content(R"({"message":"QuoteDB not initialized"})", "application/json");
            return;
        }
    }

    auto table = req.get_param_value("table");
    auto symbol = req.get_param_value("symbol");
    auto date = req.get_param_value("date");

    if (!table.empty() && !symbol.empty() && !date.empty()) {
        // 删除指定表指定标的某根 K 线（精确到日期/时刻）
        if (quoteDB.deleteBar(table, symbol, date)) {
            nlohmann::json resp;
            resp["message"] = fmt::format("Deleted bar {} for {} from {}", date, symbol, table);
            res.set_content(resp.dump(), "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"message":"Failed to delete bar"})", "application/json");
        }
    } else if (!table.empty() && !symbol.empty()) {
        // 删除指定表中的指定标的
        if (quoteDB.deleteSymbol(table, symbol)) {
            nlohmann::json resp;
            resp["message"] = fmt::format("Deleted symbol {} from {}", symbol, table);
            res.set_content(resp.dump(), "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"message":"Failed to delete symbol"})", "application/json");
        }
    } else if (!table.empty()) {
        // 删除整个表
        if (quoteDB.dropTable(table)) {
            nlohmann::json resp;
            resp["message"] = fmt::format("Table {} deleted", table);
            res.set_content(resp.dump(), "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"message":"Failed to delete table"})", "application/json");
        }
    } else {
        // 删除所有表
        auto tables = quoteDB.listTables();
        for (const auto& tbl : tables) {
            quoteDB.dropTable(tbl);
        }

        nlohmann::json resp;
        resp["message"] = fmt::format("All {} tables deleted", tables.size());
        res.set_content(resp.dump(), "application/json");
    }
}
