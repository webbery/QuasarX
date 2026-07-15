#include "Util/FinanceDB.h"
#include "Util/QuoteDB.h"
#include "Util/system.h"
#include "Util/log.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

// ═══════════════════════════════════════════════════════════
//  类别定义
// ═══════════════════════════════════════════════════════════

struct CategoryDef {
    std::string name;  // 中文名
    // CSV 列名 -> DB 列名
    std::vector<std::pair<std::string, std::string>> field_map;
};

static const std::map<std::string, CategoryDef> CATEGORY_DEFS = {
    {"profit", {
        "盈利能力",
        {
            {"roeAvg", "roe_avg"},
            {"npMargin", "np_margin"},
            {"gpMargin", "gp_margin"},
            {"netProfit", "net_profit"},
            {"epsTTM", "eps_ttm"},
            {"MBRevenue", "mb_revenue"},
            {"totalShare", "total_share"},
            {"liqaShare", "liqa_share"},
        },
    }},
    {"operation", {
        "营运能力",
        {
            {"NRTurnRatio", "nr_turn_ratio"},
            {"NRTurnDays", "nr_turn_days"},
            {"INVTurnRatio", "inv_turn_ratio"},
            {"INVTurnDays", "inv_turn_days"},
            {"CATurnRatio", "ca_turn_ratio"},
            {"ASTurnRatio", "as_turn_ratio"},
        },
    }},
    {"growth", {
        "成长能力",
        {
            {"YOYEquity", "yoy_equity"},
            {"YOYAsset", "yoy_asset"},
            {"YOYNI", "yoy_ni"},
            {"YOYEPSBasic", "yoy_eps_basic"},
            {"YOYPNI", "yoy_pni"},
        },
    }},
    {"balance", {
        "偿债能力",
        {
            {"currentRatio", "current_ratio"},
            {"quickRatio", "quick_ratio"},
            {"cashRatio", "cash_ratio"},
            {"debtToAsset", "debt_to_asset"},
        },
    }},
    {"cashflow", {
        "现金流量",
        {
            {"netProfit", "net_profit_cf"},
            {"salesService", "sales_service"},
            {"taxSurcharge", "tax_surcharge"},
            {"cashPayAcquire", "cash_pay_acquire"},
            {"netCashFlowAct", "net_cf_act"},
            {"netCashFlowInv", "net_cf_inv"},
            {"netCashFlowFin", "net_cf_fin"},
        },
    }},
    {"dupont", {
        "杜邦分析",
        {
            {"dupontROE", "dupont_roe"},
            {"dupontNetProfit", "dupont_net_profit"},
            {"dupontAssetTurn", "dupont_asset_turn"},
            {"dupontEquityMultiplier", "dupont_equity_multiplier"},
        },
    }},
};

// ═══════════════════════════════════════════════════════════
//  symbol 编解码（复用 QuoteDB 逻辑）
// ═══════════════════════════════════════════════════════════

int64_t FinanceDB::encodeSymbol(const std::string& sym) {
    return QuoteDB::encodeSymbol(sym);
}

std::string FinanceDB::decodeSymbol(int64_t encoded) {
    return QuoteDB::decodeSymbol(encoded);
}

// ═══════════════════════════════════════════════════════════
//  类别工具函数
// ═══════════════════════════════════════════════════════════

bool FinanceDB::isValidCategory(const std::string& category) {
    return CATEGORY_DEFS.count(category) > 0;
}

std::string FinanceDB::categoryName(const std::string& category) {
    auto it = CATEGORY_DEFS.find(category);
    return it != CATEGORY_DEFS.end() ? it->second.name : category;
}

const std::vector<std::pair<std::string, std::string>>&
FinanceDB::categoryFields(const std::string& category) {
    static const std::vector<std::pair<std::string, std::string>> empty;
    auto it = CATEGORY_DEFS.find(category);
    return it != CATEGORY_DEFS.end() ? it->second.field_map : empty;
}

// ═══════════════════════════════════════════════════════════
//  初始化
// ═══════════════════════════════════════════════════════════

FinanceDB& FinanceDB::instance() {
    static FinanceDB inst;
    return inst;
}

FinanceDB::~FinanceDB() {
    shutdown();
}

void FinanceDB::shutdown() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) return;
    initialized_ = false;
    if (conn_) {
        duckdb_result result;
        duckdb_query(conn_, "CHECKPOINT", &result);
        duckdb_destroy_result(&result);
        duckdb_disconnect(&conn_);
    }
    if (db_) duckdb_close(&db_);
}

bool FinanceDB::exec(const std::string& sql) {
    duckdb_result result;
    duckdb_state state = duckdb_query(conn_, sql.c_str(), &result);
    if (state != DuckDBSuccess) {
        duckdb_destroy_result(&result);
        return false;
    }
    duckdb_destroy_result(&result);
    return true;
}

bool FinanceDB::init(const std::string& db_dir) {
    if (initialized_) return true;

    std::filesystem::create_directories(db_dir);
    std::string db_path = db_dir + "/finance.db";

    char* open_error = nullptr;
    duckdb_state state = duckdb_open_ext(db_path.c_str(), &db_, nullptr, &open_error);
    if (state != DuckDBSuccess) {
        SPDLOG_ERROR("[FinanceDB] Failed to open: {}", open_error ? open_error : "unknown");
        if (open_error) duckdb_free(open_error);
        return false;
    }

    state = duckdb_connect(db_, &conn_);
    if (state != DuckDBSuccess) {
        SPDLOG_ERROR("[FinanceDB] Failed to connect");
        duckdb_close(&db_);
        db_ = nullptr;
        return false;
    }

    exec("PRAGMA threads=2");
    initialized_ = true;
    SPDLOG_INFO("[FinanceDB] Initialized at {}", db_path);
    return true;
}

// ═══════════════════════════════════════════════════════════
//  动态建表
// ═══════════════════════════════════════════════════════════

void FinanceDB::ensureTable(const std::string& category) {
    auto it = CATEGORY_DEFS.find(category);
    if (it == CATEGORY_DEFS.end()) return;

    const auto& fields = it->second.field_map;

    std::string sql = fmt::format(
        "CREATE TABLE IF NOT EXISTS {} (\n"
        "    id          INTEGER,\n"
        "    symbol      BIGINT NOT NULL,\n"
        "    stat_date   TIMESTAMP NOT NULL,\n"
        "    pub_date    TIMESTAMP,\n",
        category);

    for (const auto& [csv_col, db_col] : fields) {
        sql += fmt::format("    {} DOUBLE,\n", db_col);
    }

    sql += "    UNIQUE(symbol, stat_date)\n)";
    exec(sql);

    exec(fmt::format(
        "CREATE INDEX IF NOT EXISTS idx_{}_sym_date ON {}(symbol, stat_date)",
        category, category));
}

// ═══════════════════════════════════════════════════════════
//  CSV 导入
// ═══════════════════════════════════════════════════════════

int FinanceDB::importCsv(const std::string& csv_path, const std::string& category) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto cat_it = CATEGORY_DEFS.find(category);
    if (cat_it == CATEGORY_DEFS.end()) {
        SPDLOG_ERROR("[FinanceDB] Unknown category: {}", category);
        return -1;
    }

    ensureTable(category);

    std::ifstream ifs(csv_path);
    if (!ifs.is_open()) {
        SPDLOG_ERROR("[FinanceDB] Cannot open: {}", csv_path);
        return -1;
    }

    const auto& field_map = cat_it->second.field_map;

    // ── 阶段 1：读取 CSV header，建立列索引映射 ──
    std::string header_line;
    if (!std::getline(ifs, header_line)) {
        SPDLOG_WARN("[FinanceDB] Empty file: {}", csv_path);
        return 0;
    }
    // 去除 BOM
    if (header_line.size() >= 3 &&
        header_line[0] == '\xEF' && header_line[1] == '\xBB' && header_line[2] == '\xBF') {
        header_line = header_line.substr(3);
    }

    std::vector<std::string> csv_headers;
    {
        std::istringstream ss(header_line);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            // 去除 \r
            if (!tok.empty() && tok.back() == '\r') tok.pop_back();
            csv_headers.push_back(tok);
        }
    }

    // 找到 code, pubDate, statDate 和各字段的列索引
    int col_code = -1, col_pub = -1, col_stat = -1;
    std::map<std::string, int> field_col_idx;  // db_col_name -> csv column index

    for (size_t i = 0; i < csv_headers.size(); i++) {
        const auto& h = csv_headers[i];
        if (h == "code") col_code = static_cast<int>(i);
        else if (h == "pubDate") col_pub = static_cast<int>(i);
        else if (h == "statDate") col_stat = static_cast<int>(i);
        else {
            for (const auto& [csv_col, db_col] : field_map) {
                if (h == csv_col) {
                    field_col_idx[db_col] = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    if (col_stat < 0) {
        SPDLOG_ERROR("[FinanceDB] Missing statDate column in {}", csv_path);
        return -1;
    }

    // ── 阶段 2：解析数据行 ──
    struct Row {
        int64_t symbol;
        std::string stat_date;
        std::string pub_date;
        std::vector<double> values;  // 按 field_map 顺序，NaN 用 0 代替
        std::vector<bool> valid;     // 标记每个值是否有效
    };
    std::vector<Row> rows;
    rows.reserve(64);

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream ss(line);
        std::string tok;
        std::vector<std::string> cols;
        while (std::getline(ss, tok, ',')) cols.push_back(tok);

        Row r;
        // symbol
        if (col_code >= 0 && col_code < static_cast<int>(cols.size())) {
            r.symbol = encodeSymbol(cols[col_code]);
        } else {
            continue;
        }
        // stat_date
        if (col_stat < static_cast<int>(cols.size())) {
            r.stat_date = cols[col_stat];
        } else {
            continue;
        }
        // pub_date
        if (col_pub >= 0 && col_pub < static_cast<int>(cols.size())) {
            r.pub_date = cols[col_pub];
        }

        // 各字段值
        r.values.resize(field_map.size(), 0.0);
        r.valid.resize(field_map.size(), false);
        for (size_t fi = 0; fi < field_map.size(); fi++) {
            const auto& [csv_col, db_col] = field_map[fi];
            auto idx_it = field_col_idx.find(db_col);
            if (idx_it != field_col_idx.end() &&
                idx_it->second < static_cast<int>(cols.size())) {
                const auto& val_str = cols[idx_it->second];
                if (!val_str.empty()) {
                    try {
                        r.values[fi] = std::stod(val_str);
                        r.valid[fi] = true;
                    } catch (...) {
                        // 空值或无效值，保持 0 + false
                    }
                }
            }
        }
        rows.push_back(std::move(r));
    }
    ifs.close();

    if (rows.empty()) {
        SPDLOG_WARN("[FinanceDB] No valid rows in {}", csv_path);
        return 0;
    }

    // ── 阶段 3：构建批量 INSERT SQL ──
    std::string sql;
    sql.reserve(rows.size() * 256 + 512);

    // INSERT INTO {category} (symbol, stat_date, pub_date, field1, field2, ...) VALUES
    sql += fmt::format("INSERT INTO {} (symbol, stat_date, pub_date", category);
    for (const auto& [csv_col, db_col] : field_map) {
        sql += ", " + db_col;
    }
    sql += ") VALUES ";

    for (size_t i = 0; i < rows.size(); i++) {
        const auto& r = rows[i];
        if (i > 0) sql += ", ";

        sql += fmt::format("({},'{}','{}'", r.symbol, r.stat_date, r.pub_date);
        for (size_t fi = 0; fi < field_map.size(); fi++) {
            if (r.valid[fi]) {
                sql += fmt::format(",{:.6f}", r.values[fi]);
            } else {
                sql += ",NULL";
            }
        }
        sql += ")";
    }

    // ON CONFLICT upsert
    sql += " ON CONFLICT(symbol, stat_date) DO UPDATE SET pub_date=excluded.pub_date";
    for (const auto& [csv_col, db_col] : field_map) {
        sql += fmt::format(",{}=excluded.{}", db_col, db_col);
    }

    // ── 阶段 4：执行 ──
    exec("BEGIN TRANSACTION");
    duckdb_result result;
    duckdb_state state = duckdb_query(conn_, sql.c_str(), &result);
    if (state != DuckDBSuccess) {
        const char* err = duckdb_result_error(&result);
        SPDLOG_ERROR("[FinanceDB] Batch insert failed for {} ({}): {}",
                     category, csv_path, err ? err : "unknown");
        duckdb_destroy_result(&result);
        exec("ROLLBACK");
        return -1;
    }
    duckdb_destroy_result(&result);
    exec("COMMIT");

    int count = static_cast<int>(rows.size());
    SPDLOG_INFO("[FinanceDB] Imported {} rows into {} from {}", count, category, csv_path);
    return count;
}

// ═══════════════════════════════════════════════════════════
//  查询
// ═══════════════════════════════════════════════════════════

nlohmann::json FinanceDB::query(const std::string& category,
                                const std::string& symbol,
                                const std::string& start_date,
                                const std::string& end_date,
                                int limit) {
    std::lock_guard<std::mutex> lock(mtx_);
    nlohmann::json result;

    if (!initialized_) {
        result["error"] = "FinanceDB not initialized";
        return result;
    }

    if (!isValidCategory(category)) {
        result["error"] = fmt::format("Unknown category: {}", category);
        return result;
    }

    auto cat_it = CATEGORY_DEFS.find(category);
    const auto& field_map = cat_it->second.field_map;

    // 构建 SELECT
    std::string sql = fmt::format(
        "SELECT symbol, CAST(stat_date AS VARCHAR), CAST(pub_date AS VARCHAR)");
    for (const auto& [csv_col, db_col] : field_map) {
        sql += ", " + db_col;
    }
    sql += fmt::format(" FROM {}", category);

    bool has_where = false;
    if (!symbol.empty()) {
        int64_t sym_encoded = encodeSymbol(symbol);
        sql += fmt::format(" WHERE symbol = {}", sym_encoded);
        has_where = true;
    }
    if (!start_date.empty()) {
        sql += fmt::format("{} stat_date >= '{}'", has_where ? " AND" : " WHERE", start_date);
        has_where = true;
    }
    if (!end_date.empty()) {
        sql += fmt::format("{} stat_date <= '{}'", has_where ? " AND" : " WHERE", end_date);
    }

    sql += fmt::format(" ORDER BY stat_date ASC LIMIT {}", limit);

    duckdb_result res;
    if (duckdb_query(conn_, sql.c_str(), &res) != DuckDBSuccess) {
        const char* err = duckdb_result_error(&res);
        result["error"] = fmt::format("Query failed: {}", err ? err : "unknown");
        duckdb_destroy_result(&res);
        return result;
    }

    idx_t row_count = duckdb_row_count(&res);

    result["code"] = symbol;
    result["category"] = category;
    result["name"] = cat_it->second.name;
    result["count"] = static_cast<int>(row_count);

    nlohmann::json::array_t data;
    for (idx_t i = 0; i < row_count; i++) {
        nlohmann::json row;
        int64_t sym_encoded = duckdb_value_int64(&res, 0, i);
        row["code"] = decodeSymbol(sym_encoded);
        row["stat_date"] = duckdb_value_varchar(&res, 1, i);
        row["pub_date"] = duckdb_value_varchar(&res, 2, i);

        for (size_t fi = 0; fi < field_map.size(); fi++) {
            const auto& [csv_col, db_col] = field_map[fi];
            if (duckdb_value_is_null(&res, static_cast<idx_t>(3 + fi), i)) {
                row[db_col] = nullptr;
            } else {
                row[db_col] = duckdb_value_double(&res, static_cast<idx_t>(3 + fi), i);
            }
        }
        data.push_back(std::move(row));
    }

    result["data"] = std::move(data);
    duckdb_destroy_result(&res);
    return result;
}

// ═══════════════════════════════════════════════════════════
//  列出表 / symbols
// ═══════════════════════════════════════════════════════════

std::vector<std::string> FinanceDB::listTables() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> tables;
    if (!initialized_) return tables;

    duckdb_result res;
    if (duckdb_query(conn_,
            "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema='main' ORDER BY table_name", &res) == DuckDBSuccess) {
        idx_t rows = duckdb_row_count(&res);
        for (idx_t i = 0; i < rows; i++) {
            tables.push_back(duckdb_value_varchar(&res, 0, i));
        }
    }
    duckdb_destroy_result(&res);
    return tables;
}

std::vector<std::string> FinanceDB::listSymbols(const std::string& table) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> symbols;
    if (!initialized_) return symbols;

    std::string sql = fmt::format("SELECT DISTINCT symbol FROM {}", table);
    duckdb_result res;
    if (duckdb_query(conn_, sql.c_str(), &res) == DuckDBSuccess) {
        idx_t rows = duckdb_row_count(&res);
        for (idx_t i = 0; i < rows; i++) {
            int64_t encoded = duckdb_value_int64(&res, 0, i);
            symbols.push_back(decodeSymbol(encoded));
        }
    }
    duckdb_destroy_result(&res);
    return symbols;
}

// ═══════════════════════════════════════════════════════════
//  删除
// ═══════════════════════════════════════════════════════════

bool FinanceDB::dropTable(const std::string& table) {
    std::lock_guard<std::mutex> lock(mtx_);
    return exec(fmt::format("DROP TABLE IF EXISTS {}", table));
}

bool FinanceDB::deleteSymbol(const std::string& table, const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mtx_);
    int64_t sym_encoded = encodeSymbol(symbol);
    std::string sql = fmt::format("DELETE FROM {} WHERE symbol = {}", table, sym_encoded);
    bool ok = exec(sql);
    if (ok) {
        SPDLOG_INFO("[FinanceDB] Deleted symbol {} from {}", symbol, table);
    } else {
        SPDLOG_ERROR("[FinanceDB] Failed to delete symbol {} from {}", symbol, table);
    }
    return ok;
}
