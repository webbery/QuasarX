#include "Handler/QuoteDownloadHandler.h"
#include "Util/QuoteDB.h"
#include "Util/FinanceDB.h"
#include "Util/PythonRunner.h"
#include "Util/system.h"
#include "Util/data.h"
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

namespace fs = std::filesystem;

// 辅助：按分隔符拆分字符串
static std::vector<std::string> splitSymbols(const std::string& str, char delim = ',') {
    std::vector<std::string> result;
    std::istringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim)) {
        // 去除前后空格
        auto start = token.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        auto end = token.find_last_not_of(" \t\r\n");
        result.push_back(token.substr(start, end - start + 1));
    }
    return result;
}

// 辅助：解析脚本输出中的 JSON 进度行
static bool parseProgressLine(const std::string& line, nlohmann::json& out) {
    if (line.find("\"type\": \"download_progress\"") == std::string::npos) return false;
    try {
        out = nlohmann::json::parse(line);
        return true;
    } catch (...) {
        return false;
    }
}

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
    auto start = params.value("start", std::string(""));
    auto end = params.value("end", std::string(""));
    auto env_name = params.value("env", std::string(""));
    bool overwrite = params.value("overwrite", false);
    auto db_path = _server->GetConfig().GetDatabasePath();
    auto quote_dir = db_path + "/quote";

    // 初始化 QuoteDB
    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        if (!quoteDB.init(quote_dir, "quote.db")) {
            res.status = 500;
            res.set_content(R"({"message":"Failed to init QuoteDB"})", "application/json");
            return;
        }
    }

    // 获取 SSE socket 和 Python 环境
    nng_socket sse_sock = _server->GetSocket();
    auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
    auto interpreter = pyEnv.resolve(env_name);

    // 按标的类型分组（ETF vs Stock）
    auto sym_list = splitSymbols(symbols);
    std::vector<std::string> etf_syms, stock_syms;
    for (auto& s : sym_list) {
        std::string bs_sym = toInternalSymbol(s);
        if (is_etf(to_symbol(bs_sym))) etf_syms.push_back(bs_sym);
        else                            stock_syms.push_back(bs_sym);
    }

    std::vector<DownloadGroup> groups;
    if (!etf_syms.empty()) {
        std::string joined;
        for (auto& s : etf_syms) { if (!joined.empty()) joined += ","; joined += s; }
        groups.push_back({"etf", joined, etf_syms});
    }
    if (!stock_syms.empty()) {
        std::string joined;
        for (auto& s : stock_syms) { if (!joined.empty()) joined += ","; joined += s; }
        groups.push_back({"stock", joined, stock_syms});
    }

    if (groups.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"no valid symbols"})", "application/json");
        return;
    }

    // 复用核心下载逻辑（异步）
    runDownloadJob(sse_sock, groups, freq, start, end, env_name, quote_dir, overwrite);

    // 立即返回（返回所有分组的信息）
    nlohmann::json resp;
    resp["status"] = "started";
    nlohmann::json groups_json = nlohmann::json::array();
    for (auto& g : groups) {
        nlohmann::json g_info;
        g_info["asset_type"] = g.asset_type;
        g_info["table"] = QuoteDB::tableName(g.asset_type, freq);
        g_info["symbols"] = g.symbols_str;
        groups_json.push_back(g_info);
    }
    resp["groups"] = groups_json;
    res.set_content(resp.dump(), "application/json");
}

void QuoteDownloadHandler::runDownloadJob(nng_socket sseSock,
                                          std::vector<DownloadGroup> groups,
                                          const std::string& freq,
                                          const std::string& start,
                                          const std::string& end,
                                          const std::string& env_name,
                                          const std::string& quote_dir,
                                          bool overwrite) {
    (void)env_name;  // 调度场景固定使用系统默认 python
    std::string interpreter = "python";

    std::thread([sseSock, groups, quote_dir, freq, start, end, interpreter, overwrite]() {
        for (auto& group : groups) {
            auto t_group_start = std::chrono::steady_clock::now();
            const auto& asset_type = group.asset_type;
            auto sym_list = group.symbols;

            std::string table = QuoteDB::tableName(asset_type, freq);
            std::string hfq_dir = asset_type + "_hfq/" + freq;
            std::string org_dir = asset_type + "_org/" + freq;

            INFO("[QuoteDownload] === {} group start: {} symbols, freq={}, start={}, end={} ===",
                 asset_type, sym_list.size(), freq, start.empty() ? "max" : start, end.empty() ? "today" : end);

            SendSSE(sseSock, "quote_download", {
                {"status", "started"}, {"asset_type", asset_type},
                {"symbols", group.symbols_str}, {"total", std::to_string(sym_list.size())}
            });

            // 串行逐个标的下载 + 导入（实时推送进度）
            int downloaded = 0;
            int failed = 0;
            int total_rows = 0;
            auto& quoteDB = QuoteDB::instance();

            for (size_t i = 0; i < sym_list.size(); i++) {
                const auto& sym = sym_list[i];

                // 构建单标的下载命令
                std::string cmd = interpreter + " tools/download_etf_bs.py "
                                + sym + " " + quote_dir
                                + " --freq " + freq
                                + " --asset-type " + asset_type;
                if (!start.empty()) cmd += " --start " + start;
                if (!end.empty())   cmd += " --end " + end;

                auto t_sym_start = std::chrono::steady_clock::now();
                INFO("[QuoteDownload] [{}/{}] Downloading {} ...", i + 1, sym_list.size(), sym);

                String output;
                bool ok = RunCommand(cmd, output);
                auto t_sym_end = std::chrono::steady_clock::now();
                auto sym_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_sym_end - t_sym_start).count();

                if (!ok) {
                    failed++;
                    INFO("[QuoteDownload] [{}/{}] {} FAILED in {} ms", i + 1, sym_list.size(), sym, sym_ms);
                    SendSSE(sseSock, "quote_download", {
                        {"status", "symbol_failed"},
                        {"asset_type", asset_type},
                        {"symbol", sym},
                        {"error", "download script failed"},
                        {"downloaded", std::to_string(downloaded + failed)},
                        {"total", std::to_string(sym_list.size())},
                    });
                    continue;
                }

                // 从脚本输出中提取行数（匹配 "[OK] ... (N 条)" 格式）
                int rows = 0;
                {
                    std::istringstream iss(output);
                    std::string line;
                    while (std::getline(iss, line)) {
                        // 匹配 "([0-9]+) 条" 提取行数
                        auto pos = line.find("条)");
                        if (pos != std::string::npos) {
                            auto paren = line.rfind('(', pos);
                            if (paren != std::string::npos) {
                                try {
                                    rows = std::stoi(line.substr(paren + 1, pos - paren - 1));
                                } catch (...) {}
                            }
                        }
                    }
                }

                downloaded++;

                // Python 脚本保存文件名用 CODE.EXCHANGE 格式（如 600111.SH.csv）
                // sym 是 baostock 格式（如 sh.600111），需要转换
                std::string csv_name = sym;
                {
                    auto dot = sym.find('.');
                    if (dot != std::string::npos && dot <= 3) {
                        std::string market = sym.substr(0, dot);
                        std::string code = sym.substr(dot + 1);
                        std::transform(market.begin(), market.end(), market.begin(), ::toupper);
                        csv_name = code + "." + market;
                    }
                }

                std::string org_path = quote_dir + "/" + org_dir + "/" + csv_name + ".csv";
                std::string hfq_path = quote_dir + "/" + hfq_dir + "/" + csv_name + ".csv";

                // 从 CSV 文件获取行数（比解析脚本输出更可靠）
                if (fs::exists(org_path) || fs::exists(hfq_path)) {
                    std::string count_path = fs::exists(org_path) ? org_path : hfq_path;
                    std::ifstream ifs(count_path);
                    if (ifs.is_open()) {
                        int csv_rows = 0;
                        std::string line;
                        bool header_skipped = false;
                        while (std::getline(ifs, line)) {
                            if (line.empty()) continue;
                            if (!header_skipped) {
                                header_skipped = true;
                                continue;  // 跳过表头
                            }
                            csv_rows++;
                        }
                        rows = csv_rows;
                    }
                }

                INFO("[QuoteDownload] [{}/{}] {} done: {} rows in {} ms", i + 1, sym_list.size(), sym, rows, sym_ms);
                SendSSE(sseSock, "quote_download", {
                    {"status", "symbol_downloaded"},
                    {"asset_type", asset_type},
                    {"symbol", sym},
                    {"rows", std::to_string(rows)},
                    {"downloaded", std::to_string(downloaded + failed)},
                    {"total", std::to_string(sym_list.size())},
                });

                // 立即下载后导入
                if (!quoteDB.isInitialized()) {
                    INFO("[QuoteDownload] QuoteDB not initialized, skipping import for {}", sym);
                    continue;
                }

                if (fs::exists(org_path) || fs::exists(hfq_path)) {
                    std::string org_p = fs::exists(org_path) ? org_path : hfq_path;
                    std::string hfq_p = fs::exists(hfq_path) ? hfq_path : org_path;

                    auto t_import_start = std::chrono::steady_clock::now();
                    int imported = quoteDB.importCsv(org_p, hfq_p, table, toInternalSymbol(sym), overwrite);
                    auto t_import_end = std::chrono::steady_clock::now();
                    auto import_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_import_end - t_import_start).count();
                    INFO("[QuoteDownload] importCsv {} done: {} rows in {} ms", sym, imported, import_ms);

                    if (imported > 0) {
                        total_rows += imported;
                        SendSSE(sseSock, "quote_download", {
                            {"status", "importing"},
                            {"table", table},
                            {"symbol", sym},
                            {"rows", std::to_string(imported)}
                        });
                    }

                    // 股票除权校验
                    if (imported > 0 && table == "stock_1d") {
                        auto t_adj_start = std::chrono::steady_clock::now();
                        validateAndReconcileAdj(sseSock, table, sym);
                        auto t_adj_end = std::chrono::steady_clock::now();
                        auto adj_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_adj_end - t_adj_start).count();
                        INFO("[QuoteDownload] validateAndReconcileAdj {} done in {} ms", sym, adj_ms);
                    }

                    fs::remove(org_p);
                    fs::remove(hfq_p);
                }
            }

            auto t_group_end = std::chrono::steady_clock::now();
            auto group_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_group_end - t_group_start).count();
            INFO("[QuoteDownload] === {} group done: {} symbols ({} ok, {} failed), {} rows, total {} ms ===",
                 asset_type, sym_list.size(), downloaded, failed, total_rows, group_ms);

            SendSSE(sseSock, "quote_download", {
                {"status", "done"},
                {"asset_type", asset_type},
                {"table", table},
                {"total_rows", std::to_string(total_rows)},
                {"success", failed == 0 ? "true" : "false"}
            });
        }
    }).detach();
}

void QuoteDownloadHandler::validateAndReconcileAdj(nng_socket sseSock,
                                                   const std::string& table,
                                                   const std::string& symbol) {
    auto& quoteDB = QuoteDB::instance();
    auto& financeDB = FinanceDB::instance();
    if (!quoteDB.isInitialized() || !financeDB.isInitialized()) return;

    constexpr double REL_TOLERANCE = 0.01;  // 1% 相对差异阈值
    auto sym = toInternalSymbol(symbol);
    // 1. 读取下载的 adj_close（importCsv 已写入）
    auto downloadedBars = quoteDB.query(table, sym, "", "", 1);
    if (downloadedBars.empty()) return;
    const auto& downloaded = downloadedBars.back();

    bool downloadedValid = downloaded.adj_close > 0 && std::isfinite(downloaded.adj_close);

    // 2. 重算（基于 dividend 表，baostock 公式）
    symbol_t internalSym = to_symbol(sym);
    int64_t encodedSym = QuoteDB::encodeSymbol(sym);
    financeDB.recalcSymbolAdjPrices(get_symbol(internalSym));

    // 3. 读取重算后的 adj_close
    auto recalcBars = quoteDB.query(table, sym, "", "", 1);
    if (recalcBars.empty()) return;
    const auto& recalc = recalcBars.back();
    bool recalcValid = recalc.adj_close > 0 && std::isfinite(recalc.adj_close);

    // 4. 验证：下载值正常 → 优先使用
    if (downloadedValid) {
        if (recalcValid) {
            double maxVal = std::max(downloaded.adj_close, recalc.adj_close);
            double relDiff = std::abs(downloaded.adj_close - recalc.adj_close) / maxVal;
            if (relDiff > REL_TOLERANCE) {
                WARN("[Schedule] Adj mismatch for {}: downloaded={:.4f} recalc={:.4f} "
                     "(rel diff={:.4f}), using downloaded",
                     symbol, downloaded.adj_close, recalc.adj_close, relDiff);
                SendSSE(sseSock, "adj_reconcile", {
                    {"symbol", symbol},
                    {"downloaded", fmt::format("{:.6f}", downloaded.adj_close)},
                    {"recalc", fmt::format("{:.6f}", recalc.adj_close)},
                    {"rel_diff", fmt::format("{:.6f}", relDiff)},
                    {"action", "use_downloaded"}
                });
                restoreDownloadedAdj(table, encodedSym, downloaded);
            }
        } else {
            WARN("[Schedule] Recalc adj invalid for {}, keeping downloaded adj_close={:.4f}",
                 symbol, downloaded.adj_close);
            SendSSE(sseSock, "adj_reconcile", {
                {"symbol", symbol},
                {"downloaded", fmt::format("{:.6f}", downloaded.adj_close)},
                {"recalc", "0"},
                {"action", "use_downloaded"}
            });
            restoreDownloadedAdj(table, encodedSym, downloaded);
        }
    } else if (recalcValid) {
        INFO("[Schedule] Downloaded adj invalid for {}, keeping recalc adj_close={:.4f}",
             symbol, recalc.adj_close);
        SendSSE(sseSock, "adj_reconcile", {
            {"symbol", symbol},
            {"downloaded", "0"},
            {"recalc", fmt::format("{:.6f}", recalc.adj_close)},
            {"action", "use_recalc"}
        });
    }
}

void QuoteDownloadHandler::restoreDownloadedAdj(const std::string& table,
                                                int64_t encoded_symbol,
                                                const QuoteBar& downloaded) {
    auto& quoteDB = QuoteDB::instance();
    QuoteDB::AdjPriceUpdate u;
    u.datetime = downloaded.datetime;
    u.adj_open = downloaded.adj_open;
    u.adj_high = downloaded.adj_high;
    u.adj_low = downloaded.adj_low;
    u.adj_close = downloaded.adj_close;
    quoteDB.updateAdjPrices(table, encoded_symbol, {u});
}

void QuoteDownloadHandler::get(const httplib::Request& req, httplib::Response& res) {
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
