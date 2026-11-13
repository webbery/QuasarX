#include "Handler/MarginHandler.h"
#include "Util/FinanceDB.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "server.h"
#include <algorithm>
#include <thread>

namespace {
const char* kMarginTable = "margin_daily";

// 日期参数归一：接受 YYYY-MM-DD / YYYYMMDD，统一成 YYYY-MM-DD；非法返回空串
String NormalizeDateParam(const String& s) {
    if (IsYmdDate(s)) return s;
    if (s.size() != 8) return "";
    time_t t = FromStr(s, "%Y%m%d");
    if (t == -1) return "";
    return ToString(t, "%Y-%m-%d");
}
}  // namespace

bool MarginHandler::ensureDB(httplib::Response& res) {
    auto& financeDB = FinanceDB::instance();
    if (financeDB.isInitialized()) return true;

    auto db_path = _server->GetConfig().GetDatabasePath();
    if (!financeDB.init(db_path + "/finance", "finance.db")) {
        res.status = 500;
        res.set_content(R"({"message":"FinanceDB init failed"})", "application/json");
        return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════
//  POST /v0/margin — 下载并导入（后台线程）
// ═══════════════════════════════════════════════════════════

void MarginHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params = nlohmann::json::object();
    if (!req.body.empty()) {
        try {
            params = nlohmann::json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"message":"Invalid JSON"})", "application/json");
            return;
        }
    }

    String date = params.value("date", String(""));
    if (!date.empty()) {
        date = NormalizeDateParam(date);
        if (date.empty()) {
            res.status = 400;
            res.set_content(R"({"message":"date 格式应为 YYYYMMDD 或 YYYY-MM-DD"})",
                            "application/json");
            return;
        }
    }
    String env = params.value("env", String(""));

    Server* server = _server;

    // 抓取要联网、耗时 1~3 分钟，不能阻塞 HTTP 线程
    std::thread([server, date, env]() {
        SetCurrentThreadName("MarginDownload");
        // GetSocket() 是 thread_local：socket 必须在实际使用它的线程里创建，
        // 并由 ScopedSocket 在工作线程结束时关闭
        ScopedSocket sse(Server::GetSocket());

        SendSSE(sse.get(), "margin_download", {
            {"status", "started"},
            {"date", date.empty() ? "latest" : date}
        });

        String error;
        int rows = server->DownloadMarginData(date, env, error);
        if (rows < 0) {
            WARN("[MarginHandler] 下载失败: {}", error);
            SendSSE(sse.get(), "margin_download", {
                {"status", "failed"},
                {"error", error}
            });
            return;
        }

        SendSSE(sse.get(), "margin_download", {
            {"status", "completed"},
            {"rows", std::to_string(rows)}
        });
    }).detach();

    nlohmann::json resp;
    resp["status"] = "started";
    resp["date"] = date.empty() ? "latest" : date;
    res.set_content(resp.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  GET /v0/margin — 查询
//
//  ?symbol=sh.600000[&start][&end][&limit]   某标的历史
//  ?date=YYYY-MM-DD[&limit]                  某交易日全市场
//  无参数 / ?summary=1[&days=N]              数据概览
// ═══════════════════════════════════════════════════════════

void MarginHandler::get(const httplib::Request& req, httplib::Response& res) {
    if (!ensureDB(res)) return;
    auto& financeDB = FinanceDB::instance();

    auto symbol = req.get_param_value("symbol");
    auto date = req.get_param_value("date");
    auto start = req.get_param_value("start");
    auto end = req.get_param_value("end");
    auto limit_str = req.get_param_value("limit");

    // ── 按交易日查全市场 ──
    if (!date.empty()) {
        String dash = NormalizeDateParam(date);
        if (dash.empty()) {
            res.status = 400;
            res.set_content(R"({"message":"date 格式应为 YYYYMMDD 或 YYYY-MM-DD"})",
                            "application/json");
            return;
        }
        int limit = limit_str.empty() ? 10000 : std::stoi(limit_str);
        limit = std::min(limit, 50000);
        auto result = financeDB.query(kMarginTable, "", dash, dash, limit);
        result["date"] = dash;
        res.set_content(result.dump(), "application/json");
        return;
    }

    // ── 按标的历史 ──
    if (!symbol.empty()) {
        if (!start.empty() && !IsYmdDate(start)) {
            res.status = 400;
            res.set_content(R"({"message":"start 格式应为 YYYY-MM-DD"})", "application/json");
            return;
        }
        if (!end.empty() && !IsYmdDate(end)) {
            res.status = 400;
            res.set_content(R"({"message":"end 格式应为 YYYY-MM-DD"})", "application/json");
            return;
        }
        int limit = limit_str.empty() ? 20 : std::stoi(limit_str);
        limit = std::min(limit, 5000);
        auto result = financeDB.query(kMarginTable, toInternalSymbol(symbol),
                                      start, end, limit);
        res.set_content(result.dump(), "application/json");
        return;
    }

    // ── 概览 ──
    auto days_str = req.get_param_value("days");
    int days = days_str.empty() ? 30 : std::stoi(days_str);
    days = std::min(days, 250);
    auto result = financeDB.queryCategoryOverview(kMarginTable, days);
    res.set_content(result.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  DELETE /v0/margin — 删除
// ═══════════════════════════════════════════════════════════

void MarginHandler::del(const httplib::Request& req, httplib::Response& res) {
    if (!ensureDB(res)) return;
    auto& financeDB = FinanceDB::instance();

    auto all = req.get_param_value("all");
    auto symbol = req.get_param_value("symbol");
    auto date = req.get_param_value("date");
    auto start = req.get_param_value("start");
    auto end = req.get_param_value("end");

    // ── 清空整张表：必须显式 all=1，避免手滑把几个月的明细删光 ──
    if (all == "1" || all == "true") {
        if (!financeDB.dropTable(kMarginTable)) {
            res.status = 500;
            res.set_content(R"({"message":"Failed to drop table"})", "application/json");
            return;
        }
        nlohmann::json resp;
        resp["status"] = "dropped";
        resp["table"] = kMarginTable;
        res.set_content(resp.dump(), "application/json");
        return;
    }

    // ── 删某一天 ──
    if (!date.empty()) {
        String dash = NormalizeDateParam(date);
        if (dash.empty()) {
            res.status = 400;
            res.set_content(R"({"message":"date 格式应为 YYYYMMDD 或 YYYY-MM-DD"})",
                            "application/json");
            return;
        }
        int rows = financeDB.deleteRows(kMarginTable, "", dash, dash);
        if (rows < 0) {
            res.status = 500;
            res.set_content(R"({"message":"Failed to delete"})", "application/json");
            return;
        }
        nlohmann::json resp;
        resp["status"] = "deleted";
        resp["date"] = dash;
        resp["rows"] = rows;
        res.set_content(resp.dump(), "application/json");
        return;
    }

    // ── 删某标的（可带日期范围） ──
    if (!symbol.empty()) {
        if (!start.empty() && !IsYmdDate(start)) {
            res.status = 400;
            res.set_content(R"({"message":"start 格式应为 YYYY-MM-DD"})", "application/json");
            return;
        }
        if (!end.empty() && !IsYmdDate(end)) {
            res.status = 400;
            res.set_content(R"({"message":"end 格式应为 YYYY-MM-DD"})", "application/json");
            return;
        }
        int rows = financeDB.deleteRows(kMarginTable, toInternalSymbol(symbol), start, end);
        if (rows < 0) {
            res.status = 500;
            res.set_content(R"({"message":"Failed to delete"})", "application/json");
            return;
        }
        nlohmann::json resp;
        resp["status"] = "deleted";
        resp["symbol"] = symbol;
        resp["start"] = start;
        resp["end"] = end;
        resp["rows"] = rows;
        res.set_content(resp.dump(), "application/json");
        return;
    }

    res.status = 400;
    res.set_content(R"({"message":"需要指定 all=1 / date=YYYY-MM-DD / symbol=..."})",
                    "application/json");
}
