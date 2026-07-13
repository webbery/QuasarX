#include "Util/QuoteDB.h"
#include "Util/system.h"
#include "Util/log.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

// ═══════════════════════════════════════════════════════════
//  symbol 编解码
// ═══════════════════════════════════════════════════════════

int64_t QuoteDB::encodeSymbol(const std::string& sym) {
    symbol_t s = to_symbol(sym);
    int64_t val = 0;
    std::memcpy(&val, &s, sizeof(symbol_t));
    return val;
}

std::string QuoteDB::decodeSymbol(int64_t encoded) {
    symbol_t s;
    std::memcpy(&s, &encoded, sizeof(symbol_t));
    return get_symbol(s);
}

// ═══════════════════════════════════════════════════════════
//  表名 / 频率
// ═══════════════════════════════════════════════════════════

std::string QuoteDB::normalizeFreq(const std::string& freq) {
    if (freq == "daily") return "1d";
    return freq;  // 5m, 15m, 30m, 60m 保持不变
}

std::string QuoteDB::tableName(const std::string& asset_type, const std::string& freq) {
    return asset_type + "_" + normalizeFreq(freq);
}

// ═══════════════════════════════════════════════════════════
//  初始化
// ═══════════════════════════════════════════════════════════

QuoteDB& QuoteDB::instance() {
    static QuoteDB inst;
    return inst;
}

QuoteDB::~QuoteDB() {
    std::lock_guard<std::mutex> lock(mtx_);
    initialized_ = false;  // 阻止析构后新线程再调用
    if (conn_) duckdb_disconnect(&conn_);
    if (db_) duckdb_close(&db_);
}

bool QuoteDB::exec(const std::string& sql) {
    duckdb_result result;
    duckdb_state state = duckdb_query(conn_, sql.c_str(), &result);
    if (state != DuckDBSuccess) {
        duckdb_destroy_result(&result);
        return false;
    }
    duckdb_destroy_result(&result);
    return true;
}

bool QuoteDB::init(const std::string& db_dir) {
    if (initialized_) return true;

    std::filesystem::create_directories(db_dir);
    std::string db_path = db_dir + "/quote.db";

    char* open_error = nullptr;
    duckdb_state state = duckdb_open_ext(db_path.c_str(), &db_, nullptr, &open_error);
    if (state != DuckDBSuccess) {
        SPDLOG_ERROR("[QuoteDB] Failed to open: {}", open_error ? open_error : "unknown");
        if (open_error) duckdb_free(open_error);
        return false;
    }

    state = duckdb_connect(db_, &conn_);
    if (state != DuckDBSuccess) {
        SPDLOG_ERROR("[QuoteDB] Failed to connect");
        duckdb_close(&db_);
        db_ = nullptr;
        return false;
    }

    exec("PRAGMA threads=2");
    initialized_ = true;
    SPDLOG_INFO("[QuoteDB] Initialized at {}", db_path);
    return true;
}

void QuoteDB::ensureTable(const std::string& table) {
    std::string sql = fmt::format(R"(
        CREATE TABLE IF NOT EXISTS {} (
            id          INTEGER,
            symbol      BIGINT NOT NULL,
            datetime    TIMESTAMP NOT NULL,
            open        DOUBLE,
            close       DOUBLE,
            high        DOUBLE,
            low         DOUBLE,
            volume      BIGINT,
            turnover    DOUBLE,
            ext         TINYINT DEFAULT 0,
            adj_open    DOUBLE,
            adj_close   DOUBLE,
            adj_high    DOUBLE,
            adj_low     DOUBLE,
            UNIQUE(symbol, datetime)
        )
    )", table);
    exec(sql);

    // 索引
    exec(fmt::format("CREATE INDEX IF NOT EXISTS idx_{}_sym_time ON {}(symbol, datetime)", table, table));
    exec(fmt::format("CREATE INDEX IF NOT EXISTS idx_{}_time ON {}(datetime DESC)", table, table));
}

// ═══════════════════════════════════════════════════════════
//  CSV 导入
// ═══════════════════════════════════════════════════════════

int QuoteDB::importCsv(const std::string& csv_path,
                       const std::string& table,
                       const std::string& symbol_str,
                       AdjType adj) {
    std::lock_guard<std::mutex> lock(mtx_);

    ensureTable(table);

    std::ifstream ifs(csv_path);
    if (!ifs.is_open()) {
        SPDLOG_ERROR("[QuoteDB] Cannot open: {}", csv_path);
        return -1;
    }

    int64_t sym_encoded = encodeSymbol(symbol_str);
    bool is_hfq = (adj == AdjType::HFQ);

    // ── 阶段 1：解析 CSV，累积所有行 ──
    struct Row {
        std::string datetime;
        double open, close, high, low, turnover;
        int64_t volume;
        uint8_t ext;
    };
    std::vector<Row> rows;
    rows.reserve(2048);

    std::string line;
    bool headerSkipped = false;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        if (line.size() >= 3 && line[0] == '\xEF' && line[1] == '\xBB' && line[2] == '\xBF')
            line = line.substr(3);

        std::istringstream ss(line);
        std::string tok;
        std::vector<std::string> cols;
        while (std::getline(ss, tok, ',')) cols.push_back(tok);
        if (cols.size() < 7) continue;

        if (!headerSkipped) {
            std::string first = cols[0];
            std::transform(first.begin(), first.end(), first.begin(), ::tolower);
            if (first == "datetime" || first == "date") {
                headerSkipped = true;
                continue;
            }
            headerSkipped = true;
        }

        try {
            Row r;
            r.datetime = cols[0];
            // 如果 datetime 只有日期部分（YYYY-MM-DD），补充时间部分
            if (r.datetime.length() == 10 && r.datetime[4] == '-' && r.datetime[7] == '-') {
                r.datetime += " 00:00:00";
            }
            r.open   = std::stod(cols[1]);
            r.close  = std::stod(cols[2]);
            r.high   = std::stod(cols[3]);
            r.low    = std::stod(cols[4]);
            r.volume = static_cast<int64_t>(std::stod(cols[5]));
            r.turnover = std::stod(cols[6]);
            r.ext = (r.volume == 0 || (r.open == 0 && r.close == 0 && r.high == 0 && r.low == 0)) ? 0x01 : 0;
            rows.push_back(std::move(r));
        } catch (const std::exception& e) {
            SPDLOG_WARN("[QuoteDB] Skip invalid row: {} ({})", line, e.what());
        }
    }
    ifs.close();

    if (rows.empty()) {
        SPDLOG_WARN("[QuoteDB] No valid rows in {}", csv_path);
        return 0;
    }

    // ── 阶段 2：构建批量 INSERT SQL ──
    // 预留空间：每行约 120 字符
    std::string sql;
    sql.reserve(rows.size() * 128 + 512);

    if (is_hfq) {
        sql += fmt::format(
            "INSERT INTO {} (symbol, datetime, adj_open, adj_close, adj_high, adj_low, volume, turnover, ext) VALUES ",
            table);
    } else {
        sql += fmt::format(
            "INSERT INTO {} (symbol, datetime, open, close, high, low, volume, turnover, ext) VALUES ",
            table);
    }

    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (i > 0) sql += ", ";
        sql += fmt::format("({},'{}',{:.4f},{:.4f},{:.4f},{:.4f},{},{:.2f},{})",
                           sym_encoded, r.datetime,
                           r.open, r.close, r.high, r.low,
                           r.volume, r.turnover, static_cast<int>(r.ext));
    }

    // ON CONFLICT upsert
    if (is_hfq) {
        sql += " ON CONFLICT(symbol, datetime) DO UPDATE SET "
               "adj_open=excluded.adj_open, adj_close=excluded.adj_close, "
               "adj_high=excluded.adj_high, adj_low=excluded.adj_low, "
               "volume=excluded.volume, turnover=excluded.turnover, ext=excluded.ext";
    } else {
        sql += " ON CONFLICT(symbol, datetime) DO UPDATE SET "
               "open=excluded.open, close=excluded.close, high=excluded.high, "
               "low=excluded.low, volume=excluded.volume, turnover=excluded.turnover, ext=excluded.ext";
    }

    // ── 阶段 3：单次执行 ──
    exec("BEGIN TRANSACTION");
    duckdb_result result;
    duckdb_state state = duckdb_query(conn_, sql.c_str(), &result);
    if (state != DuckDBSuccess) {
        const char* err = duckdb_result_error(&result);
        SPDLOG_ERROR("[QuoteDB] Batch insert failed for {} ({}): {}",
                     symbol_str, table, err ? err : "unknown");
        duckdb_destroy_result(&result);
        exec("ROLLBACK");
        return -1;
    }
    duckdb_destroy_result(&result);
    exec("COMMIT");

    int count = static_cast<int>(rows.size());
    SPDLOG_INFO("[QuoteDB] Imported {} rows into {} for {} (adj={})",
                count, table, symbol_str, is_hfq ? "HFQ" : "None");
    return count;
}

// ═══════════════════════════════════════════════════════════
//  查询
// ═══════════════════════════════════════════════════════════

std::vector<QuoteBar> QuoteDB::query(const std::string& table,
                                     const std::string& symbol,
                                     const std::string& start_time,
                                     const std::string& end_time,
                                     int limit) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<QuoteBar> result;
    if (!initialized_) return result;

    int64_t sym_encoded = encodeSymbol(symbol);

    std::string sql = fmt::format(
        "SELECT symbol, CAST(datetime AS VARCHAR), open, close, high, low, volume, turnover, ext, "
        "adj_open, adj_close, adj_high, adj_low "
        "FROM {} WHERE symbol = {}", table, sym_encoded);

    if (!start_time.empty())
        sql += fmt::format(" AND datetime >= '{}'", start_time);
    if (!end_time.empty())
        sql += fmt::format(" AND datetime <= '{}'", end_time);

    sql += fmt::format(" ORDER BY datetime ASC LIMIT {}", limit);

    duckdb_result res;
    if (duckdb_query(conn_, sql.c_str(), &res) != DuckDBSuccess) {
        duckdb_destroy_result(&res);
        return result;
    }

    idx_t row_count = duckdb_row_count(&res);
    for (idx_t i = 0; i < row_count; i++) {
        QuoteBar bar;
        bar.symbol   = symbol;  // 直接用请求的 symbol 字符串
        bar.datetime = duckdb_value_varchar(&res, 1, i);
        bar.open     = duckdb_value_double(&res, 2, i);
        bar.close    = duckdb_value_double(&res, 3, i);
        bar.high     = duckdb_value_double(&res, 4, i);
        bar.low      = duckdb_value_double(&res, 5, i);
        bar.volume   = duckdb_value_int64(&res, 6, i);
        bar.turnover = duckdb_value_double(&res, 7, i);
        bar.ext      = static_cast<uint8_t>(duckdb_value_int8(&res, 8, i));
        // 后复权价格（NULL 时 duckdb_value_double 返回 0.0）
        bar.adj_open  = duckdb_value_double(&res, 9, i);
        bar.adj_close = duckdb_value_double(&res, 10, i);
        bar.adj_high  = duckdb_value_double(&res, 11, i);
        bar.adj_low   = duckdb_value_double(&res, 12, i);
        result.push_back(std::move(bar));
    }

    duckdb_destroy_result(&res);
    return result;
}

// ═══════════════════════════════════════════════════════════
//  列出表 / symbols
// ═══════════════════════════════════════════════════════════

std::vector<std::string> QuoteDB::listTables() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> tables;
    if (!initialized_) return tables;

    duckdb_result res;
    if (duckdb_query(conn_, "SELECT table_name FROM information_schema.tables WHERE table_schema='main' ORDER BY table_name", &res) == DuckDBSuccess) {
        idx_t rows = duckdb_row_count(&res);
        for (idx_t i = 0; i < rows; i++) {
            tables.push_back(duckdb_value_varchar(&res, 0, i));
        }
    }
    duckdb_destroy_result(&res);
    return tables;
}

std::vector<std::string> QuoteDB::listSymbols(const std::string& table) {
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
//  删除表
// ═══════════════════════════════════════════════════════════

bool QuoteDB::dropTable(const std::string& table) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::string sql = fmt::format("DROP TABLE IF EXISTS {}", table);
    return exec(sql);
}

// ═══════════════════════════════════════════════════════════
//  删除标的 / 查询时间范围
// ═══════════════════════════════════════════════════════════

bool QuoteDB::deleteSymbol(const std::string& table, const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mtx_);
    int64_t sym_encoded = encodeSymbol(symbol);
    std::string sql = fmt::format("DELETE FROM {} WHERE symbol = {}", table, sym_encoded);
    bool ok = exec(sql);
    if (!ok) {
        SPDLOG_ERROR("[QuoteDB] Failed to delete symbol {} from {}", symbol, table);
    } else {
        SPDLOG_INFO("[QuoteDB] Deleted symbol {} from {}", symbol, table);
    }
    return ok;
}

std::vector<QuoteDB::SymbolTimeRange> QuoteDB::getSymbolTimeRanges(const std::string& table) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<SymbolTimeRange> result;
    if (!initialized_) return result;

    std::string sql = fmt::format(
        "SELECT symbol, MIN(datetime), MAX(datetime), COUNT(*) FROM {} GROUP BY symbol ORDER BY symbol",
        table);

    duckdb_result db_result;
    if (duckdb_query(conn_, sql.c_str(), &db_result) != DuckDBSuccess) {
        duckdb_destroy_result(&db_result);
        return result;
    }

    idx_t rows = duckdb_row_count(&db_result);
    for (idx_t i = 0; i < rows; i++) {
        SymbolTimeRange range;
        int64_t encoded = duckdb_value_int64(&db_result, 0, i);
        // 将 int64_t 直接拷贝到 symbol_t（size 相同）
        std::memcpy(&range.symbol, &encoded, sizeof(symbol_t));
        range.start_time = duckdb_value_varchar(&db_result, 1, i);
        range.end_time = duckdb_value_varchar(&db_result, 2, i);
        range.count = static_cast<int64_t>(duckdb_value_int64(&db_result, 3, i));
        result.push_back(std::move(range));
    }

    duckdb_destroy_result(&db_result);
    return result;
}
