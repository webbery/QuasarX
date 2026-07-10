#include "Handler/FinanceDataHandler.h"
#include "Util/FinanceDB.h"
#include "Util/data.h"
#include "server.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <fmt/format.h>

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════
//  POST /v0/finance/data — 导入/导出财务数据
// ═══════════════════════════════════════════════════════════

void FinanceDataHandler::post(const httplib::Request& req, httplib::Response& res) {
    FinanceDataRequest data_req;
    std::string error_msg;
    if (!parseRequest(req.body, data_req, error_msg)) {
        res.status = 400;
        nlohmann::json resp;
        resp["error"] = error_msg;
        res.set_content(resp.dump(), "application/json");
        return;
    }

    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!financeDB.init(db_path + "/finance")) {
            res.status = 500;
            res.set_content(R"({"error":"FinanceDB not initialized"})", "application/json");
            return;
        }
    }

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
        resp["category"] = data_req.category;
        resp["code"] = data_req.code;
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

        if (data_req.format == "json") {
            auto result = financeDB.query(data_req.category, data_req.code,
                                          data_req.start_date, data_req.end_date);
            res.set_content(result.dump(), "application/json");
        } else {
            res.set_header("Content-Type", "text/csv");
            res.set_header("Content-Disposition",
                          fmt::format("attachment; filename={}_{}.csv",
                                      data_req.code, data_req.category));
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
//  DELETE /v0/finance/data — 清理财务数据
// ═══════════════════════════════════════════════════════════

void FinanceDataHandler::del(const httplib::Request& req, httplib::Response& res) {
    FinanceDataRequest data_req;
    std::string error_msg;
    if (!parseRequest(req.body, data_req, error_msg)) {
        // DEL 允许不提供 action，默认 cleanup
        try {
            auto json = nlohmann::json::parse(req.body);
            data_req.category = json.value("category", std::string(""));
            data_req.code = json.value("code", std::string(""));
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
            return;
        }
    }
    data_req.action = "cleanup";

    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!financeDB.init(db_path + "/finance")) {
            res.status = 500;
            res.set_content(R"({"error":"FinanceDB not initialized"})", "application/json");
            return;
        }
    }

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
//  私有方法
// ═══════════════════════════════════════════════════════════

bool FinanceDataHandler::parseRequest(const std::string& json_str,
                                      FinanceDataRequest& req,
                                      std::string& error_msg) {
    try {
        auto json = nlohmann::json::parse(json_str);

        req.action = json.value("action", "");
        req.category = json.value("category", "");
        req.code = json.value("code", "");
        req.start_date = json.value("start_date", "");
        req.end_date = json.value("end_date", "");
        req.format = json.value("format", "csv");
        req.csv_lines = json.value("data", std::vector<std::string>());

        if (req.action.empty()) {
            error_msg = "Missing required field: action (import/export/cleanup)";
            return false;
        }

        if (req.action != "cleanup" && req.category.empty()) {
            error_msg = "Missing required field: category";
            return false;
        }
        if (req.action == "import" && req.csv_lines.empty()) {
            error_msg = "Missing required field: data (CSV lines)";
            return false;
        }
        if (req.action == "export" && req.code.empty()) {
            error_msg = "Missing required field: code";
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        error_msg = fmt::format("Invalid JSON: {}", e.what());
        return false;
    }
}

bool FinanceDataHandler::importCsv(const FinanceDataRequest& req,
                                   int& imported_rows,
                                   std::string& error_msg) {
    try {
        std::string name = fmt::format("{}_{}", req.code, req.category);
        auto tmp_path = DataUtil::WriteTempCsv(req.csv_lines, name);
        if (tmp_path.empty()) {
            error_msg = "Failed to create temp CSV file";
            return false;
        }

        auto& financeDB = FinanceDB::instance();
        imported_rows = financeDB.importCsv(tmp_path, req.category);

        DataUtil::CleanupTempFile(tmp_path);

        if (imported_rows < 0) {
            error_msg = "FinanceDB::importCsv failed";
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        error_msg = fmt::format("Import failed: {}", e.what());
        return false;
    }
}

bool FinanceDataHandler::exportCsv(const FinanceDataRequest& req,
                                   std::string& output,
                                   std::string& error_msg) {
    try {
        auto& financeDB = FinanceDB::instance();
        auto result = financeDB.query(req.category, req.code,
                                      req.start_date, req.end_date, 100000);

        if (result.contains("error") || result.value("count", 0) == 0) {
            error_msg = fmt::format("No data found for {} in {}", req.code, req.category);
            return false;
        }

        // 构建 CSV
        std::ostringstream oss;
        const auto& fields = FinanceDB::categoryFields(req.category);

        // header
        oss << "code,stat_date,pub_date";
        for (const auto& [csv_col, db_col] : fields) {
            oss << "," << db_col;
        }
        oss << "\n";

        // data rows
        for (const auto& row : result["data"]) {
            oss << row.value("code", "") << ","
                << row.value("stat_date", "") << ","
                << row.value("pub_date", "");
            for (const auto& [csv_col, db_col] : fields) {
                if (row.contains(db_col) && !row[db_col].is_null()) {
                    oss << "," << row[db_col].get<double>();
                } else {
                    oss << ",";
                }
            }
            oss << "\n";
        }

        output = oss.str();
        return true;
    } catch (const std::exception& e) {
        error_msg = fmt::format("Export failed: {}", e.what());
        return false;
    }
}

bool FinanceDataHandler::cleanup(const FinanceDataRequest& req,
                                 std::string& message,
                                 std::string& error_msg) {
    try {
        auto& financeDB = FinanceDB::instance();

        DataUtil::DBCleanupOps ops;
        ops.delete_symbol = [&financeDB](const std::string& t, const std::string& s) {
            return financeDB.deleteSymbol(t, s);
        };
        ops.drop_table = [&financeDB](const std::string& t) {
            return financeDB.dropTable(t);
        };
        ops.list_tables = [&financeDB]() {
            auto all = financeDB.listTables();
            // 只返回有效的财务类别表
            std::vector<std::string> filtered;
            for (const auto& t : all) {
                if (FinanceDB::isValidCategory(t)) filtered.push_back(t);
            }
            return filtered;
        };

        auto [ok, msg] = DataUtil::CleanupDBData(req.category, req.code, ops);
        if (ok) {
            message = msg;
            return true;
        } else {
            error_msg = msg;
            return false;
        }
    } catch (const std::exception& e) {
        error_msg = fmt::format("Cleanup failed: {}", e.what());
        return false;
    }
}
