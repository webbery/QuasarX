#include "Util/QuoteDB.h"
#include "Util/system.h"
#include "Util/log.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>

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
                       const std::string& symbol_str) {
    std::lock_guard<std::mutex> lock(mtx_);

    ensureTable(table);

    std::ifstream ifs(csv_path);
    if (!ifs.is_open()) {
        SPDLOG_ERROR("[QuoteDB] Cannot open: {}", csv_path);
        return -1;
    }

    int64_t sym_encoded = encodeSymbol(symbol_str);

    // 跳过 header
    std::string header;
    std::getline(ifs, header);

    std::string insert_sql = fmt::format(
        "INSERT INTO {} (symbol, datetime, open, close, high, low, volume, turnover, ext) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(symbol, datetime) DO UPDATE SET "
        "open=excluded.open, close=excluded.close, high=excluded.high, "
        "low=excluded.low, volume=excluded.volume, turnover=excluded.turnover, ext=excluded.ext",
        table);

    exec("BEGIN TRANSACTION");

    duckdb_prepared_statement stmt = nullptr;
    if (duckdb_prepare(conn_, insert_sql.c_str(), &stmt) != DuckDBSuccess) {
        SPDLOG_ERROR("[QuoteDB] Prepare failed for table {}", table);
        exec("ROLLBACK");
        return -1;
    }

    int count = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;

        // 解析 CSV: datetime,open,close,high,low,volume,turnover
        std::istringstream ss(line);
        std::string tok;
        std::vector<std::string> cols;
        while (std::getline(ss, tok, ',')) cols.push_back(tok);
        if (cols.size() < 7) continue;

        const auto& dt_str   = cols[0];
        double open   = std::stod(cols[1]);
        double close  = std::stod(cols[2]);
        double high   = std::stod(cols[3]);
        double low    = std::stod(cols[4]);
        int64_t volume = static_cast<int64_t>(std::stod(cols[5]));
        double turnover = std::stod(cols[6]);

        // 停牌检测
        uint8_t ext = 0;
        if (volume == 0 || (open == 0 && close == 0 && high == 0 && low == 0)) {
            ext |= 0x01;
        }

        // 绑定参数
        duckdb_bind_int64(stmt, 1, sym_encoded);
        duckdb_bind_varchar(stmt, 2, dt_str.c_str());
        duckdb_bind_double(stmt, 3, open);
        duckdb_bind_double(stmt, 4, close);
        duckdb_bind_double(stmt, 5, high);
        duckdb_bind_double(stmt, 6, low);
        duckdb_bind_int64(stmt, 7, volume);
        duckdb_bind_double(stmt, 8, turnover);
        duckdb_bind_int8(stmt, 9, static_cast<int8_t>(ext));

        if (duckdb_execute_prepared(stmt, nullptr) == DuckDBSuccess) {
            count++;
        }
    }

    duckdb_destroy_prepare(&stmt);
    exec("COMMIT");

    SPDLOG_INFO("[QuoteDB] Imported {} rows into {} for {}", count, table, symbol_str);
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
        "SELECT symbol, CAST(datetime AS VARCHAR), open, close, high, low, volume, turnover, ext "
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
