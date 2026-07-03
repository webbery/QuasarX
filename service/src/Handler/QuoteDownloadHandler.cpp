#include "Handler/QuoteDownloadHandler.h"
#include "Util/QuoteDB.h"
#include "Util/PythonRunner.h"
#include "Util/system.h"
#include "server.h"
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

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
    auto asset_type = params.value("asset_type", std::string("etf"));
    auto start = params.value("start", std::string(""));
    auto end = params.value("end", std::string(""));
    auto env_name = params.value("env", std::string(""));
    auto db_path = _server->GetConfig().GetDatabasePath();
    auto quote_dir = db_path + "/quote";

    // 初始化 QuoteDB
    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        if (!quoteDB.init(quote_dir)) {
            res.status = 500;
            res.set_content(R"({"message":"Failed to init QuoteDB"})", "application/json");
            return;
        }
    }

    // 获取 SSE socket 和 Python 环境
    nng_socket sse_sock = _server->GetSocket();
    auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
    auto interpreter = pyEnv.resolve(env_name);

    // 构建命令
    std::string cmd = interpreter + " tools/download_etf_bs.py "
                    + symbols + " " + quote_dir
                    + " --freq " + freq
                    + " --asset-type " + asset_type;
    if (!start.empty()) cmd += " --start " + start;
    if (!end.empty())   cmd += " --end " + end;

    std::string table = QuoteDB::tableName(asset_type, freq);
    std::string freq_dir = asset_type + "_hfq/" + freq;

    // 后台线程执行下载 + 导入
    std::thread([sse_sock, cmd, table, quote_dir, freq_dir, symbols]() {
        SetCurrentThreadName("QuoteDownload");

        SendSSE(sse_sock, "quote_download", {
            {"status", "started"}, {"symbols", symbols}
        });

        // 执行下载脚本
        String output;
        bool ok = RunCommand(cmd, output);

        SendSSE(sse_sock, "quote_download", {
            {"status", ok ? "downloaded" : "download_failed"},
            {"output", output}
        });

        // 扫描 CSV 并导入 DuckDB
        int total_rows = 0;
        std::string scan_dir = quote_dir + "/" + freq_dir;

        if (ok && fs::exists(scan_dir)) {
            for (auto& entry : fs::directory_iterator(scan_dir)) {
                if (entry.path().extension() != ".csv") continue;

                std::string sym = entry.path().stem().string();
                int rows = QuoteDB::instance().importCsv(
                    entry.path().string(), table, sym);

                if (rows > 0) {
                    total_rows += rows;
                    SendSSE(sse_sock, "quote_download", {
                        {"status", "importing"},
                        {"table", table},
                        {"symbol", sym},
                        {"rows", std::to_string(rows)}
                    });
                }

                // 导入后删除 CSV
                fs::remove(entry.path());
            }
        }

        SendSSE(sse_sock, "quote_download", {
            {"status", "done"},
            {"table", table},
            {"total_rows", std::to_string(total_rows)},
            {"success", ok ? "true" : "false"}
        });
    }).detach();

    // 立即返回
    nlohmann::json resp;
    resp["status"] = "started";
    resp["table"] = table;
    resp["symbols"] = symbols;
    res.set_content(resp.dump(), "application/json");
}

void QuoteDownloadHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto table = req.get_param_value("table");
    auto symbol = req.get_param_value("symbol");
    if (table.empty() || symbol.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing 'table' or 'symbol'"})", "application/json");
        return;
    }

    auto start = req.get_param_value("start");
    auto end = req.get_param_value("end");
    auto limit_str = req.get_param_value("limit");
    int limit = limit_str.empty() ? 5000 : std::stoi(limit_str);

    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!quoteDB.init(db_path + "/quote")) {
            res.status = 500;
            res.set_content(R"({"message":"QuoteDB not initialized"})", "application/json");
            return;
        }
    }

    auto bars = quoteDB.query(table, symbol, start, end, limit);

    nlohmann::json result;
    result["table"] = table;
    result["symbol"] = symbol;
    result["count"] = bars.size();
    result["data"] = nlohmann::json::array();

    for (auto& bar : bars) {
        result["data"].push_back({
            {"datetime", bar.datetime},
            {"open", bar.open},
            {"close", bar.close},
            {"high", bar.high},
            {"low", bar.low},
            {"volume", bar.volume},
            {"turnover", bar.turnover},
            {"suspended", (bar.ext & 0x01) != 0}
        });
    }

    res.set_content(result.dump(), "application/json");
}
