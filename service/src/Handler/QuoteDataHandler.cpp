#include "Handler/QuoteDataHandler.h"
#include "Util/QuoteDB.h"
#include "Util/data.h"
#include "server.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "json.hpp"
#include <fmt/format.h>

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════
//  POST /v0/quote/data — 导入/导出行情数据
// ═══════════════════════════════════════════════════════════

void QuoteDataHandler::post(const httplib::Request& req, httplib::Response& res) {
    // 解析 JSON 请求
    QuoteDataRequest data_req;
    std::string error_msg;
    if (!parseRequest(req.body, data_req, error_msg)) {
        res.status = 400;
        nlohmann::json resp;
        resp["error"] = error_msg;
        res.set_content(resp.dump(), "application/json");
        return;
    }

    // 初始化 QuoteDB（如果未初始化）
    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!quoteDB.init(db_path + "/quote")) {
            res.status = 500;
            res.set_content(R"({"error":"QuoteDB not initialized"})", "application/json");
            return;
        }
    }

    // 根据 action 路由
    if (data_req.action == "import") {
        int imported_rows = 0;
        if (!importCsv(data_req, imported_rows, error_msg)) {
            res.status = 500;
            nlohmann::json resp;
            resp["error"] = error_msg;
            res.set_content(resp.dump(), "application/json");
            return;
        }

        nlohmann::json resp;
        resp["message"] = "Import successful";
        resp["table"] = data_req.table;
        resp["symbol"] = data_req.symbol;
        resp["imported_rows"] = imported_rows;
        res.set_content(resp.dump(), "application/json");

    } else if (data_req.action == "export") {
        std::string output;
        if (!exportCsv(data_req, output, error_msg)) {
            res.status = 500;
            nlohmann::json resp;
            resp["error"] = error_msg;
            res.set_content(resp.dump(), "application/json");
            return;
        }

        // 根据格式返回
        if (data_req.format == "json") {
            nlohmann::json resp;
            resp["table"] = data_req.table;
            resp["symbol"] = data_req.symbol;
            resp["data"] = output;
            res.set_content(resp.dump(), "application/json");
        } else {
            res.set_header("Content-Type", "text/csv");
            res.set_header("Content-Disposition",
                          fmt::format("attachment; filename={}_{}.csv", data_req.symbol, data_req.table));
            res.set_content(output, "text/csv");
        }

    } else {
        res.status = 400;
        nlohmann::json resp;
        resp["error"] = "Invalid action. Use 'import' or 'export'";
        res.set_content(resp.dump(), "application/json");
    }
}

// ═══════════════════════════════════════════════════════════
//  DELETE /v0/quote/data — 清理测试数据
// ═══════════════════════════════════════════════════════════

void QuoteDataHandler::del(const httplib::Request& req, httplib::Response& res) {
    // 解析 JSON 请求
    QuoteDataRequest data_req;
    std::string error_msg;
    if (!parseRequest(req.body, data_req, error_msg)) {
        res.status = 400;
        nlohmann::json resp;
        resp["error"] = error_msg;
        res.set_content(resp.dump(), "application/json");
        return;
    }

    data_req.action = "cleanup";

    // 初始化 QuoteDB
    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!quoteDB.init(db_path + "/quote")) {
            res.status = 500;
            res.set_content(R"({"error":"QuoteDB not initialized"})", "application/json");
            return;
        }
    }

    // 清理数据
    std::string message;
    if (!cleanup(data_req, message, error_msg)) {
        res.status = 500;
        nlohmann::json resp;
        resp["error"] = error_msg;
        res.set_content(resp.dump(), "application/json");
        return;
    }

    nlohmann::json resp;
    resp["message"] = message;
    res.set_content(resp.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  私有方法实现
// ═══════════════════════════════════════════════════════════

bool QuoteDataHandler::parseRequest(const std::string& json_str,
                                    QuoteDataRequest& req,
                                    std::string& error_msg) {
    try {
        auto json = nlohmann::json::parse(json_str);

        req.action = json.value("action", "");
        req.table = json.value("table", "");
        req.symbol = json.value("symbol", "");
        req.adj_type = json.value("adj", "hfq");
        req.start_time = json.value("start_time", "");
        req.end_time = json.value("end_time", "");
        req.format = json.value("format", "csv");
        req.csv_lines = json.value("data", std::vector<std::string>());

        if (req.action.empty()) {
            error_msg = "Missing required field: action (import/export/cleanup)";
            return false;
        }

        if (req.action != "cleanup" && req.table.empty()) {
            error_msg = "Missing required field: table";
            return false;
        }
        if (req.action != "cleanup" && req.symbol.empty()) {
            error_msg = "Missing required field: symbol";
            return false;
        }
        if (req.action == "import" && req.csv_lines.empty()) {
            error_msg = "Missing required field: data (CSV lines)";
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        error_msg = fmt::format("Invalid JSON: {}", e.what());
        return false;
    }
}

bool QuoteDataHandler::importCsv(const QuoteDataRequest& req,
                                 int& imported_rows,
                                 std::string& error_msg) {
    try {
        // 创建临时 CSV 文件
        auto tmp_dir = fs::temp_directory_path() / "quasarx_test";
        fs::create_directories(tmp_dir);
        auto tmp_csv = tmp_dir / fmt::format("{}_{}.csv", req.symbol, req.table);

        // 写入 CSV 内容
        {
            std::ofstream ofs(tmp_csv.string());
            if (!ofs.is_open()) {
                error_msg = fmt::format("Failed to create temp file: {}", tmp_csv.string());
                return false;
            }

            for (const auto& line : req.csv_lines) {
                ofs << line << "\n";
            }
            ofs.close();
        }

        // 调用 QuoteDB 导入
        auto& quoteDB = QuoteDB::instance();
        AdjType adj = (req.adj_type == "none") ? AdjType::None : AdjType::HFQ;
        imported_rows = quoteDB.importCsv(tmp_csv.string(), req.table, req.symbol, adj);

        // 删除临时文件
        fs::remove(tmp_csv);

        if (imported_rows < 0) {
            error_msg = "QuoteDB::importCsv failed";
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        error_msg = fmt::format("Import failed: {}", e.what());
        return false;
    }
}

bool QuoteDataHandler::exportCsv(const QuoteDataRequest& req,
                                 std::string& output,
                                 std::string& error_msg) {
    try {
        auto& quoteDB = QuoteDB::instance();

        // 查询数据
        auto bars = quoteDB.query(req.table, req.symbol, req.start_time, req.end_time, 100000);
        if (bars.empty()) {
            error_msg = fmt::format("No data found for symbol {} in table {}", req.symbol, req.table);
            return false;
        }

        // 构建 CSV
        std::ostringstream oss;
        oss << "date,open,high,low,close,volume,turnover,adj_open,adj_high,adj_low,adj_close\n";

        for (const auto& bar : bars) {
            // datetime 格式：只保留日期部分
            std::string date_str = bar.datetime.substr(0, 10);

            oss << fmt::format("{},{:.2f},{:.2f},{:.2f},{:.2f},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f}\n",
                             date_str,
                             bar.open, bar.high, bar.low, bar.close,
                             bar.volume, bar.turnover,
                             bar.adj_open, bar.adj_high, bar.adj_low, bar.adj_close);
        }

        output = oss.str();
        return true;
    } catch (const std::exception& e) {
        error_msg = fmt::format("Export failed: {}", e.what());
        return false;
    }
}

bool QuoteDataHandler::cleanup(const QuoteDataRequest& req,
                               std::string& message,
                               std::string& error_msg) {
    try {
        auto& quoteDB = QuoteDB::instance();

        if (!req.table.empty() && !req.symbol.empty()) {
            // 删除指定表中的指定标的
            if (quoteDB.deleteSymbol(req.table, req.symbol)) {
                message = fmt::format("Deleted symbol {} from {}", req.symbol, req.table);
                return true;
            } else {
                error_msg = fmt::format("Failed to delete symbol {} from {}", req.symbol, req.table);
                return false;
            }
        } else if (!req.table.empty()) {
            // 删除整个表
            if (quoteDB.dropTable(req.table)) {
                message = fmt::format("Table {} deleted", req.table);
                return true;
            } else {
                error_msg = fmt::format("Failed to delete table {}", req.table);
                return false;
            }
        } else if (!req.symbol.empty()) {
            // 仅删除 symbol：列出所有表，删除该 symbol
            auto tables = quoteDB.listTables();
            int deleted_count = 0;
            for (const auto& tbl : tables) {
                if (quoteDB.deleteSymbol(tbl, req.symbol)) {
                    deleted_count++;
                }
            }
            message = fmt::format("Deleted symbol {} from {} tables", req.symbol, deleted_count);
            return true;
        } else {
            error_msg = "At least one param required: table or symbol";
            return false;
        }
    } catch (const std::exception& e) {
        error_msg = fmt::format("Cleanup failed: {}", e.what());
        return false;
    }
}
