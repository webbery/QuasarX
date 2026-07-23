#include "Util/FinanceDB.h"
#include "Util/QuoteDB.h"
#include "Util/system.h"
#include "Util/log.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <cstring>
#include <algorithm>

// ═══════════════════════════════════════════════════════════
//  类别定义
// ═══════════════════════════════════════════════════════════

struct CategoryDef {
    String name;  // 中文名
    // CSV 列名 -> DB 列名
    Vector<std::pair<String, String>> field_map;
};

static const std::map<String, CategoryDef> CATEGORY_DEFS = {
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

int64_t FinanceDB::encodeSymbol(const String& sym) {
    return QuoteDB::encodeSymbol(sym);
}

String FinanceDB::decodeSymbol(int64_t encoded) {
    return QuoteDB::decodeSymbol(encoded);
}

// ═══════════════════════════════════════════════════════════
//  类别工具函数
// ═══════════════════════════════════════════════════════════

bool FinanceDB::isValidCategory(const String& category) {
    return CATEGORY_DEFS.count(category) > 0;
}

String FinanceDB::categoryName(const String& category) {
    auto it = CATEGORY_DEFS.find(category);
    return it != CATEGORY_DEFS.end() ? it->second.name : category;
}

const Vector<std::pair<String, String>>&
FinanceDB::categoryFields(const String& category) {
    static const Vector<std::pair<String, String>> empty;
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
    if (!initialized_) return;
    initialized_ = false;
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (conn_) {
        duckdb_result result;
        duckdb_query(conn_, "CHECKPOINT", &result);
        duckdb_destroy_result(&result);
        duckdb_disconnect(&conn_);
    }
    if (db_) duckdb_close(&db_);
}

bool FinanceDB::exec(const String& sql) {
    duckdb_result result;
    duckdb_state state = duckdb_query(conn_, sql.c_str(), &result);
    if (state != DuckDBSuccess) {
        duckdb_destroy_result(&result);
        return false;
    }
    duckdb_destroy_result(&result);
    return true;
}

bool FinanceDB::init(const String& db_dir) {
    if (initialized_) return true;

    std::filesystem::create_directories(db_dir);
    String db_path = db_dir + "/finance.db";

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

void FinanceDB::ensureTable(const String& category) {
    auto it = CATEGORY_DEFS.find(category);
    if (it == CATEGORY_DEFS.end()) return;

    const auto& fields = it->second.field_map;

    String sql = fmt::format(
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

int FinanceDB::importCsv(const String& csv_path, const String& category) {
    auto cat_it = CATEGORY_DEFS.find(category);
    if (cat_it == CATEGORY_DEFS.end()) {
        SPDLOG_ERROR("[FinanceDB] Unknown category: {}", category);
        return -1;
    }

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    ensureTable(category);

    std::ifstream ifs(csv_path);
    if (!ifs.is_open()) {
        SPDLOG_ERROR("[FinanceDB] Cannot open: {}", csv_path);
        return -1;
    }

    const auto& field_map = cat_it->second.field_map;

    // ── 阶段 1：读取 CSV header，建立列索引映射 ──
    String header_line;
    if (!std::getline(ifs, header_line)) {
        SPDLOG_WARN("[FinanceDB] Empty file: {}", csv_path);
        return 0;
    }
    // 去除 BOM
    if (header_line.size() >= 3 &&
        header_line[0] == '\xEF' && header_line[1] == '\xBB' && header_line[2] == '\xBF') {
        header_line = header_line.substr(3);
    }

    Vector<String> csv_headers;
    {
        std::istringstream ss(header_line);
        String tok;
        while (std::getline(ss, tok, ',')) {
            // 去除 \r
            if (!tok.empty() && tok.back() == '\r') tok.pop_back();
            csv_headers.push_back(tok);
        }
    }

    // 找到 code, pubDate, statDate 和各字段的列索引
    int col_code = -1, col_pub = -1, col_stat = -1;
    std::map<String, int> field_col_idx;  // db_col_name -> csv column index

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
        String stat_date;
        String pub_date;
        Vector<double> values;  // 按 field_map 顺序，NaN 用 0 代替
        Vector<bool> valid;     // 标记每个值是否有效
    };
    Vector<Row> rows;
    rows.reserve(64);

    String line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream ss(line);
        String tok;
        Vector<String> cols;
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
    String sql;
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

nlohmann::json FinanceDB::query(const String& category,
                                const String& symbol,
                                const String& start_date,
                                const String& end_date,
                                int limit) {
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
    String sql = fmt::format(
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

    std::lock_guard<std::recursive_mutex> lock(mtx_);
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

Vector<String> FinanceDB::listTables() {
    Vector<String> tables;
    if (!initialized_) return tables;

    std::lock_guard<std::recursive_mutex> lock(mtx_);
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

Vector<String> FinanceDB::listSymbols(const String& table) {
    Vector<String> symbols;
    if (!initialized_) return symbols;

    String sql = fmt::format("SELECT DISTINCT symbol FROM {}", table);
    duckdb_result res;
    std::lock_guard<std::recursive_mutex> lock(mtx_);
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

bool FinanceDB::dropTable(const String& table) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    return exec(fmt::format("DROP TABLE IF EXISTS {}", table));
}

bool FinanceDB::deleteSymbol(const String& table, const String& symbol) {
    int64_t sym_encoded = encodeSymbol(symbol);
    String sql = fmt::format("DELETE FROM {} WHERE symbol = {}", table, sym_encoded);
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    bool ok = exec(sql);
    if (ok) {
        SPDLOG_INFO("[FinanceDB] Deleted symbol {} from {}", symbol, table);
    } else {
        SPDLOG_ERROR("[FinanceDB] Failed to delete symbol {} from {}", symbol, table);
    }
    return ok;
}

// ═══════════════════════════════════════════════════════════
//  dividend 表 — 建表
// ═══════════════════════════════════════════════════════════

void FinanceDB::ensureDividendTable() {
    exec(
        "CREATE TABLE IF NOT EXISTS dividend ("
        "  id               INTEGER,"
        "  symbol           BIGINT NOT NULL,"
        "  announce_date    TIMESTAMP,"
        "  report_year      VARCHAR,"
        "  ex_dividend_date TIMESTAMP NOT NULL,"
        "  record_date      TIMESTAMP,"
        "  implement_date   TIMESTAMP,"
        "  bonus_per_10     DOUBLE DEFAULT 0,"
        "  transfer_per_10  DOUBLE DEFAULT 0,"
        "  cash_per_10      DOUBLE DEFAULT 0,"
        "  allot_per_10     DOUBLE DEFAULT 0,"
        "  allot_price      DOUBLE DEFAULT 0,"
        "  ex_div_price     DOUBLE,"
        "  action_type      TINYINT DEFAULT 0,"
        "  UNIQUE(symbol, ex_dividend_date)"
        ")"
    );
    exec("CREATE INDEX IF NOT EXISTS idx_dividend_ex_date "
         "ON dividend(ex_dividend_date DESC)");
    exec("CREATE INDEX IF NOT EXISTS idx_dividend_symbol "
         "ON dividend(symbol, ex_dividend_date)");
}

// ═══════════════════════════════════════════════════════════
//  dividend 表 — CSV 导入
// ═══════════════════════════════════════════════════════════

int FinanceDB::importDividendCsv(const String& csv_path) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    ensureDividendTable();

    std::ifstream ifs(csv_path);
    if (!ifs.is_open()) {
        SPDLOG_ERROR("[FinanceDB] Cannot open: {}", csv_path);
        return -1;
    }

    // ── 解析 header，建立列索引 ──
    String header_line;
    if (!std::getline(ifs, header_line)) {
        SPDLOG_WARN("[FinanceDB] Empty file: {}", csv_path);
        return 0;
    }
    // 去 BOM
    if (header_line.size() >= 3 &&
        header_line[0] == '\xEF' && header_line[1] == '\xBB' && header_line[2] == '\xBF') {
        header_line = header_line.substr(3);
    }

    Vector<String> csv_headers;
    {
        std::istringstream ss(header_line);
        String tok;
        while (std::getline(ss, tok, ',')) {
            if (!tok.empty() && tok.back() == '\r') tok.pop_back();
            csv_headers.push_back(tok);
        }
    }

    // 列名 → 索引映射
    auto colIdx = [&](const String& name) -> int {
        for (size_t i = 0; i < csv_headers.size(); i++) {
            if (csv_headers[i] == name) return static_cast<int>(i);
        }
        return -1;
    };

    int c_symbol   = colIdx("symbol");
    int c_announce = colIdx("announce_date");
    int c_year     = colIdx("report_year");
    int c_ex_div   = colIdx("ex_dividend_date");
    int c_record   = colIdx("record_date");
    int c_impl     = colIdx("implement_date");
    int c_bonus    = colIdx("bonus_per_10");
    int c_transfer = colIdx("transfer_per_10");
    int c_cash     = colIdx("cash_per_10");
    int c_allot    = colIdx("allot_per_10");
    int c_allot_p  = colIdx("allot_price");
    int c_ex_price = colIdx("ex_div_price");
    int c_action   = colIdx("action_type");

    if (c_symbol < 0 || c_ex_div < 0) {
        SPDLOG_ERROR("[FinanceDB] dividend CSV missing required columns (symbol/ex_dividend_date): {}", csv_path);
        return -1;
    }

    // ── 解析数据行 ──
    struct DivRow {
        int64_t symbol;
        String announce_date;
        String report_year;
        String ex_dividend_date;
        String record_date;
        String implement_date;
        double bonus_per_10;
        double transfer_per_10;
        double cash_per_10;
        double allot_per_10;
        double allot_price;
        String ex_div_price;
        int action_type;
    };
    Vector<DivRow> rows;
    rows.reserve(64);

    auto safeDouble = [](const String& s) -> double {
        if (s.empty()) return 0.0;
        try { return std::stod(s); } catch (...) { return 0.0; }
    };
    auto safeInt = [](const String& s) -> int {
        if (s.empty()) return 0;
        try { return std::stoi(s); } catch (...) { return 0; }
    };
    auto getField = [&](const Vector<String>& cols, int idx) -> String {
        if (idx >= 0 && idx < static_cast<int>(cols.size())) return cols[idx];
        return "";
    };

    String line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream ss(line);
        String tok;
        Vector<String> cols;
        while (std::getline(ss, tok, ',')) cols.push_back(tok);

        DivRow r{};
        String sym_str = getField(cols, c_symbol);
        if (sym_str.empty()) continue;
        r.symbol = encodeSymbol(sym_str);

        r.announce_date   = getField(cols, c_announce);
        r.report_year     = getField(cols, c_year);
        r.ex_dividend_date = getField(cols, c_ex_div);
        r.record_date     = getField(cols, c_record);
        r.implement_date  = getField(cols, c_impl);
        r.bonus_per_10    = safeDouble(getField(cols, c_bonus));
        r.transfer_per_10 = safeDouble(getField(cols, c_transfer));
        r.cash_per_10     = safeDouble(getField(cols, c_cash));
        r.allot_per_10    = safeDouble(getField(cols, c_allot));
        r.allot_price     = safeDouble(getField(cols, c_allot_p));
        r.ex_div_price    = getField(cols, c_ex_price);
        r.action_type     = safeInt(getField(cols, c_action));

        if (r.ex_dividend_date.empty()) continue;
        rows.push_back(std::move(r));
    }
    ifs.close();

    if (rows.empty()) {
        SPDLOG_WARN("[FinanceDB] No valid rows in {}", csv_path);
        return 0;
    }

    // ── 构建批量 INSERT ──
    String sql;
    sql.reserve(rows.size() * 320 + 512);
    sql +=
        "INSERT INTO dividend (symbol, announce_date, report_year, ex_dividend_date, "
        "record_date, implement_date, bonus_per_10, transfer_per_10, cash_per_10, "
        "allot_per_10, allot_price, ex_div_price, action_type) VALUES ";

    for (size_t i = 0; i < rows.size(); i++) {
        const auto& r = rows[i];
        if (i > 0) sql += ", ";

        sql += fmt::format("({},", r.symbol);

        // 可空 TIMESTAMP 字段
        auto tsVal = [](const String& s) -> String {
            return s.empty() ? "NULL" : fmt::format("'{}'", s);
        };
        sql += fmt::format("{},'{}',{},{},{},",
                           tsVal(r.announce_date), r.report_year,
                           tsVal(r.ex_dividend_date),
                           tsVal(r.record_date), tsVal(r.implement_date));

        // DOUBLE 字段
        sql += fmt::format("{},{},{},{},{},",
                           r.bonus_per_10, r.transfer_per_10, r.cash_per_10,
                           r.allot_per_10, r.allot_price);

        // ex_div_price (可空)
        sql += r.ex_div_price.empty() ? "NULL," : fmt::format("{}," , r.ex_div_price);

        // action_type
        sql += fmt::format("{})", r.action_type);
    }

    sql += " ON CONFLICT(symbol, ex_dividend_date) DO UPDATE SET "
           "announce_date=excluded.announce_date, report_year=excluded.report_year, "
           "record_date=excluded.record_date, implement_date=excluded.implement_date, "
           "bonus_per_10=excluded.bonus_per_10, transfer_per_10=excluded.transfer_per_10, "
           "cash_per_10=excluded.cash_per_10, allot_per_10=excluded.allot_per_10, "
           "allot_price=excluded.allot_price, ex_div_price=excluded.ex_div_price, "
           "action_type=excluded.action_type";

    // ── 执行 ──
    exec("BEGIN TRANSACTION");
    duckdb_result result;
    duckdb_state state = duckdb_query(conn_, sql.c_str(), &result);
    if (state != DuckDBSuccess) {
        const char* err = duckdb_result_error(&result);
        SPDLOG_ERROR("[FinanceDB] dividend insert failed ({}): {}", csv_path, err ? err : "unknown");
        duckdb_destroy_result(&result);
        exec("ROLLBACK");
        return -1;
    }
    duckdb_destroy_result(&result);
    exec("COMMIT");

    int count = static_cast<int>(rows.size());
    SPDLOG_INFO("[FinanceDB] Imported {} rows into dividend from {}", count, csv_path);
    return count;
}

// ═══════════════════════════════════════════════════════════
//  dividend 表 — 批量导入目录下所有 CSV
// ═══════════════════════════════════════════════════════════

int FinanceDB::importAllDividends(const String& dividend_dir) {
    if (!std::filesystem::exists(dividend_dir)) {
        SPDLOG_INFO("[FinanceDB] dividend directory not found, creating: {}", dividend_dir);
        std::filesystem::create_directories(dividend_dir);
    }

    int total = 0;
    for (auto& entry : std::filesystem::directory_iterator(dividend_dir)) {
        if (!entry.is_regular_file()) continue;
        auto filename = entry.path().filename().string();
        // 匹配 {symbol}_dividend.csv
        if (filename.size() > 13 &&
            filename.substr(filename.size() - 13) == "_dividend.csv") {
            int n = importDividendCsv(entry.path().string());
            if (n > 0) total += n;
        }
    }

    SPDLOG_INFO("[FinanceDB] Imported {} total dividend rows from {}", total, dividend_dir);
    return total;
}

// ═══════════════════════════════════════════════════════════
//  dividend 表 — 按日期查询
// ═══════════════════════════════════════════════════════════

nlohmann::json FinanceDB::queryDividendByDate(const String& date) {
    nlohmann::json result;

    if (!initialized_) {
        result["error"] = "FinanceDB not initialized";
        return result;
    }

    String sql = fmt::format(
        "SELECT symbol, CAST(announce_date AS VARCHAR), report_year, "
        "CAST(ex_dividend_date AS VARCHAR), CAST(record_date AS VARCHAR), "
        "CAST(implement_date AS VARCHAR), "
        "bonus_per_10, transfer_per_10, cash_per_10, "
        "allot_per_10, allot_price, ex_div_price, action_type "
        "FROM dividend WHERE ex_dividend_date = '{}' "
        "ORDER BY symbol", date);

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    duckdb_result res;
    if (duckdb_query(conn_, sql.c_str(), &res) != DuckDBSuccess) {
        const char* err = duckdb_result_error(&res);
        result["error"] = fmt::format("Query failed: {}", err ? err : "unknown");
        duckdb_destroy_result(&res);
        return result;
    }

    idx_t row_count = duckdb_row_count(&res);
    result["date"] = date;
    result["has_corporate_action"] = row_count > 0;
    result["count"] = static_cast<int>(row_count);

    nlohmann::json::array_t data;
    for (idx_t i = 0; i < row_count; i++) {
        nlohmann::json row;
        row["symbol"] = decodeSymbol(duckdb_value_int64(&res, 0, i));
        row["announce_date"] = duckdb_value_varchar(&res, 1, i);
        row["report_year"] = duckdb_value_varchar(&res, 2, i);
        row["ex_dividend_date"] = duckdb_value_varchar(&res, 3, i);
        row["record_date"] = duckdb_value_varchar(&res, 4, i);
        row["implement_date"] = duckdb_value_varchar(&res, 5, i);
        row["bonus_per_10"] = duckdb_value_double(&res, 6, i);
        row["transfer_per_10"] = duckdb_value_double(&res, 7, i);
        row["cash_per_10"] = duckdb_value_double(&res, 8, i);
        row["allot_per_10"] = duckdb_value_double(&res, 9, i);
        row["allot_price"] = duckdb_value_double(&res, 10, i);
        if (!duckdb_value_is_null(&res, 11, i))
            row["ex_div_price"] = duckdb_value_double(&res, 11, i);
        else
            row["ex_div_price"] = nullptr;
        row["action_type"] = duckdb_value_int8(&res, 12, i);
        data.push_back(std::move(row));
    }

    result["data"] = std::move(data);
    duckdb_destroy_result(&res);
    return result;
}

// ═══════════════════════════════════════════════════════════
//  dividend 表 — 按标的查询
// ═══════════════════════════════════════════════════════════

nlohmann::json FinanceDB::queryDividendBySymbol(const String& symbol,
                                                 const String& start_date,
                                                 const String& end_date) {
    nlohmann::json result;

    if (!initialized_) {
        result["error"] = "FinanceDB not initialized";
        return result;
    }

    int64_t sym_encoded = encodeSymbol(symbol);
    String sql = fmt::format(
        "SELECT CAST(announce_date AS VARCHAR), report_year, "
        "CAST(ex_dividend_date AS VARCHAR), CAST(record_date AS VARCHAR), "
        "CAST(implement_date AS VARCHAR), "
        "bonus_per_10, transfer_per_10, cash_per_10, "
        "allot_per_10, allot_price, ex_div_price, action_type "
        "FROM dividend WHERE symbol = {}", sym_encoded);

    if (!start_date.empty())
        sql += fmt::format(" AND ex_dividend_date >= '{}'", start_date);
    if (!end_date.empty())
        sql += fmt::format(" AND ex_dividend_date <= '{}'", end_date);

    sql += " ORDER BY ex_dividend_date ASC";

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    duckdb_result res;
    if (duckdb_query(conn_, sql.c_str(), &res) != DuckDBSuccess) {
        const char* err = duckdb_result_error(&res);
        result["error"] = fmt::format("Query failed: {}", err ? err : "unknown");
        duckdb_destroy_result(&res);
        return result;
    }

    idx_t row_count = duckdb_row_count(&res);
    result["code"] = symbol;
    result["count"] = static_cast<int>(row_count);

    nlohmann::json::array_t data;
    for (idx_t i = 0; i < row_count; i++) {
        nlohmann::json row;
        row["announce_date"] = duckdb_value_varchar(&res, 0, i);
        row["report_year"] = duckdb_value_varchar(&res, 1, i);
        row["ex_dividend_date"] = duckdb_value_varchar(&res, 2, i);
        row["record_date"] = duckdb_value_varchar(&res, 3, i);
        row["implement_date"] = duckdb_value_varchar(&res, 4, i);
        row["bonus_per_10"] = duckdb_value_double(&res, 5, i);
        row["transfer_per_10"] = duckdb_value_double(&res, 6, i);
        row["cash_per_10"] = duckdb_value_double(&res, 7, i);
        row["allot_per_10"] = duckdb_value_double(&res, 8, i);
        row["allot_price"] = duckdb_value_double(&res, 9, i);
        if (!duckdb_value_is_null(&res, 10, i))
            row["ex_div_price"] = duckdb_value_double(&res, 10, i);
        else
            row["ex_div_price"] = nullptr;
        row["action_type"] = duckdb_value_int8(&res, 11, i);
        data.push_back(std::move(row));
    }

    result["data"] = std::move(data);
    duckdb_destroy_result(&res);
    return result;
}

// ═══════════════════════════════════════════════════════════
//  dividend 表驱动的后复权价格计算
//  算法: BaoStock 涨跌幅复权法
// ═══════════════════════════════════════════════════════════

nlohmann::json FinanceDB::DividendEvent::toJson() const {
    return {
        {"symbol", symbol},
        {"ex_dividend_date", static_cast<int64_t>(ex_dividend_date)},
        {"cash_per_10", cash_per_10},
        {"bonus_per_10", bonus_per_10},
        {"transfer_per_10", transfer_per_10},
        {"ex_div_price", ex_div_price},
        {"action_type", action_type},
    };
}

double FinanceDB::calcEventAdjFactor(double prev_close, const DividendEvent& event) {
    if (prev_close <= 0) return 1.0;

    double ex_ref_price = event.ex_div_price;

    // ex_div_price 缺失时反推
    //   ex_ref_price ≈ prev_close * (1 - cash_per_10/10/prev_close + (bonus+transfer)/10)
    if (ex_ref_price <= 0) {
        double cash_ratio  = event.cash_per_10 / 10.0 / prev_close;
        double stock_ratio = (event.bonus_per_10 + event.transfer_per_10) / 10.0;
        ex_ref_price = prev_close * (1.0 - cash_ratio + stock_ratio);
    }

    if (ex_ref_price <= 0) return 1.0;
    return prev_close / ex_ref_price;
}

Vector<FinanceDB::DividendEvent> FinanceDB::getDividendEvents(const String& symbol) {
    Vector<DividendEvent> result;

    if (!initialized_) return result;
    int64_t sym = encodeSymbol(symbol);

    String sql = fmt::format(
        "SELECT epoch(ex_dividend_date), cash_per_10, bonus_per_10, "
        "transfer_per_10, ex_div_price, action_type "
        "FROM dividend WHERE symbol = {} ORDER BY ex_dividend_date ASC", sym);

    SPDLOG_INFO("[FinanceDB] getDividendEvents: symbol='{}' encoded={}", symbol, sym);

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    duckdb_result res;
    if (duckdb_query(conn_, sql.c_str(), &res) != DuckDBSuccess) {
        const char* err = duckdb_result_error(&res);
        SPDLOG_ERROR("[FinanceDB] getDividendEvents query FAILED: {}", err ? err : "unknown");
        duckdb_destroy_result(&res);
        return result;
    }

    idx_t rows = duckdb_row_count(&res);
    SPDLOG_INFO("[FinanceDB] getDividendEvents: rows={}", rows);
    result.reserve(rows);
    for (idx_t i = 0; i < rows; i++) {
        DividendEvent e;
        e.symbol = symbol;
        e.ex_dividend_date = static_cast<time_t>(duckdb_value_int64(&res, 0, i));
        e.cash_per_10    = duckdb_value_double(&res, 1, i);
        e.bonus_per_10   = duckdb_value_double(&res, 2, i);
        e.transfer_per_10= duckdb_value_double(&res, 3, i);
        e.action_type    = duckdb_value_int8(&res, 5, i);
        if (!duckdb_value_is_null(&res, 4, i))
            e.ex_div_price = duckdb_value_double(&res, 4, i);
        result.push_back(std::move(e));
    }
    duckdb_destroy_result(&res);
    return result;
}

int FinanceDB::recalcSymbolAdjPrices(const String& symbol) {

    if (!initialized_) {
        SPDLOG_ERROR("[FinanceDB] recalcSymbolAdjPrices: not initialized");
        return -1;
    }

    // 1. 取该标的的所有分红事件
    auto events = getDividendEvents(symbol);
    if (events.empty()) {
        SPDLOG_WARN("[FinanceDB] No dividend events for {}, adj_* unchanged", symbol);
        return 0;
    }

    int64_t sym = encodeSymbol(symbol);

    // 2. 打开独立连接直接访问 quote.db（不用 ATTACH/DETACH，避免状态泄漏）
    std::lock_guard<std::recursive_mutex> lock(mtx_);

    duckdb_database quote_db = nullptr;
    duckdb_connection quote_conn = nullptr;
    if (duckdb_open("data/quote/quote.db", &quote_db) != DuckDBSuccess) {
        SPDLOG_ERROR("[FinanceDB] Failed to open quote.db");
        return -1;
    }
    if (duckdb_connect(quote_db, &quote_conn) != DuckDBSuccess) {
        SPDLOG_ERROR("[FinanceDB] Failed to connect to quote.db");
        duckdb_close(&quote_db);
        return -1;
    }
    SPDLOG_INFO("[FinanceDB] recalc: opened quote.db OK, sym_encoded={}", sym);

    // RAII 守卫：保证函数退出时一定关闭连接
    struct QuoteConnGuard {
        duckdb_connection* conn;
        duckdb_database* db;
        ~QuoteConnGuard() {
            if (conn && *conn) duckdb_disconnect(conn);
            if (db && *db) duckdb_close(db);
        }
    } connGuard{&quote_conn, &quote_db};

    // 3. 读取 stock_1d 中该标的的所有 bar (按日期升序)
    String read_sql = fmt::format(
        "SELECT epoch(datetime), open, close, high, low "
        "FROM stock_1d WHERE symbol = {} ORDER BY datetime ASC", sym);

    duckdb_result res;
    if (duckdb_query(quote_conn, read_sql.c_str(), &res) != DuckDBSuccess) {
        SPDLOG_ERROR("[FinanceDB] recalcSymbolAdjPrices: read failed for {}",
                     duckdb_result_error(&res));
        duckdb_destroy_result(&res);
        return -1;
    }

    idx_t n = duckdb_row_count(&res);
    SPDLOG_INFO("[FinanceDB] recalc: SELECT returned {} rows", n);
    if (n == 0) {
        duckdb_destroy_result(&res);
        return 0;
    }

    // 4. 对每个事件，找到 ex_date 之前最近一根 bar 的 close 作为 prev_close
    Vector<double> prev_closes(events.size(), 0.0);
    for (size_t i = 0; i < events.size(); i++) {
        for (idx_t j = 0; j < n; j++) {
            time_t bar_t = static_cast<time_t>(duckdb_value_int64(&res, 0, j));
            if (bar_t < events[i].ex_dividend_date) {
                prev_closes[i] = duckdb_value_double(&res, 2, j);  // close
            } else {
                break;
            }
        }
        SPDLOG_INFO("[FinanceDB] recalc: event[{}] ex_date={} prev_close={}",
                     i, static_cast<int64_t>(events[i].ex_dividend_date), prev_closes[i]);
    }

    // 5. 为每根 bar 计算累乘因子
    struct BarData {
        time_t datetime;
        double open, close, high, low;
        double adj_factor;
    };
    Vector<BarData> bars;
    bars.reserve(n);

    for (idx_t i = 0; i < n; i++) {
        BarData b;
        b.datetime = static_cast<time_t>(duckdb_value_int64(&res, 0, i));
        b.open  = duckdb_value_double(&res, 1, i);
        b.close = duckdb_value_double(&res, 2, i);
        b.high  = duckdb_value_double(&res, 3, i);
        b.low   = duckdb_value_double(&res, 4, i);
        b.adj_factor = 1.0;

        for (size_t e = 0; e < events.size(); e++) {
            if (events[e].ex_dividend_date > b.datetime && prev_closes[e] > 0) {
                b.adj_factor *= calcEventAdjFactor(prev_closes[e], events[e]);
            }
        }
        bars.push_back(b);
    }
    duckdb_destroy_result(&res);

    if (!bars.empty()) {
        SPDLOG_INFO("[FinanceDB] recalc: bar[0] dt={} open={} adj_factor={:.6f}",
                     static_cast<int64_t>(bars[0].datetime), bars[0].open, bars[0].adj_factor);
    }

    // 6. 通过 QuoteDB 自身连接写回复权价格（避免跨连接 MVCC 不可见）
    Vector<QuoteDB::AdjPriceUpdate> updates;
    updates.reserve(bars.size());
    for (auto& b : bars) {
        QuoteDB::AdjPriceUpdate u;
        char timebuf[32];
        struct tm tm_val;
        time_t t = b.datetime;
        gmtime_r(&t, &tm_val);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_val);
        u.datetime = timebuf;
        u.adj_open  = b.open * b.adj_factor;
        u.adj_close = b.close * b.adj_factor;
        u.adj_high  = b.high * b.adj_factor;
        u.adj_low   = b.low * b.adj_factor;
        updates.push_back(std::move(u));
    }

    int updated = QuoteDB::instance().updateAdjPrices("stock_1d", sym, updates);

    SPDLOG_INFO("[FinanceDB] Recalculated adj prices for {}: {} bars, {} events",
                symbol, updated, events.size());
    return updated;
}

nlohmann::json FinanceDB::recalcAllAdjPrices() {
    nlohmann::json result;

    if (!initialized_) {
        result["error"] = "FinanceDB not initialized";
        return result;
    }

    Vector<String> symbols = listSymbols("dividend");
    int total_bars = 0;
    nlohmann::json::array_t errors;

    for (auto& sym : symbols) {
        int n = recalcSymbolAdjPrices(sym);
        if (n < 0) errors.push_back(sym);
        else total_bars += n;
    }

    result["symbols"] = static_cast<int>(symbols.size());
    result["bars"] = total_bars;
    result["errors"] = errors;
    SPDLOG_INFO("[FinanceDB] recalcAllAdjPrices: {} symbols, {} bars, {} errors",
                symbols.size(), total_bars, errors.size());
    return result;
}
