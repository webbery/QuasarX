#include "Util/QuoteDB.h"
#include "Util/system.h"
#include "Util/datetime.h"
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
    return freq;
}

std::string QuoteDB::tableName(const std::string& asset_type, const std::string& freq) {
    return asset_type + "_" + normalizeFreq(freq);
}

// ═══════════════════════════════════════════════════════════
//  单例 / ensureTables
// ═══════════════════════════════════════════════════════════

QuoteDB& QuoteDB::instance() {
    static QuoteDB inst;
    return inst;
}

void QuoteDB::ensureTables() {
    // QuoteDB 表按需创建（importCsv/upsertBar 调 ensureTable），此处不做集中建表
}

// ═══════════════════════════════════════════════════════════
//  按需建表（子类内部使用）
// ═══════════════════════════════════════════════════════════

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
    exec_unsafe(sql);

    exec_unsafe(fmt::format("CREATE INDEX IF NOT EXISTS idx_{}_sym_time ON {}(symbol, datetime)", table, table));
    exec_unsafe(fmt::format("CREATE INDEX IF NOT EXISTS idx_{}_time ON {}(datetime DESC)", table, table));
}

// ═══════════════════════════════════════════════════════════
//  CSV 导入
// ═══════════════════════════════════════════════════════════

int QuoteDB::importCsv(const std::string& org_csv_path,
                       const std::string& hfq_csv_path,
                       const std::string& table,
                       const std::string& symbol_str) {
    auto t_start = std::chrono::high_resolution_clock::now();

    std::lock_guard<std::recursive_mutex> lock(mtx());

    ensureTable(table);

    int64_t sym_encoded = encodeSymbol(symbol_str);

    struct Row {
        std::string datetime;
        double open, close, high, low, turnover;
        int64_t volume;
        uint8_t ext;
    };

    // 解析 CSV → Map<datetime, Row>（按 datetime 对齐合并）
    auto parseCsv = [](const std::string& path, Map<String, Row>& out) -> int {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            SPDLOG_ERROR("[QuoteDB] Cannot open: {}", path);
            return -1;
        }

        std::string line;
        bool headerSkipped = false;
        int count = 0;
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
                out[r.datetime] = std::move(r);
                ++count;
            } catch (const std::exception& e) {
                SPDLOG_WARN("[QuoteDB] Skip invalid row: {} ({})", line, e.what());
            }
        }
        return count;
    };

    // ── 1. 解析两份 CSV ──
    Map<String, Row> org_rows, hfq_rows;
    int org_count = parseCsv(org_csv_path, org_rows);
    int hfq_count = parseCsv(hfq_csv_path, hfq_rows);

    auto t_parse = std::chrono::high_resolution_clock::now();
    auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_parse - t_start).count();
    SPDLOG_INFO("[QuoteDB] CSV parse: org={} hfq={} in {}ms", org_count, hfq_count, parse_ms);

    if (org_rows.empty() && hfq_rows.empty()) {
        SPDLOG_WARN("[QuoteDB] No valid rows in {} / {}", org_csv_path, hfq_csv_path);
        return 0;
    }

    // ── 2. 合并：原始列取 org，adj 列取 hfq，datetime 取并集 ──
    struct MergedRow {
        std::string datetime;
        double open = 0, close = 0, high = 0, low = 0;      // 原始
        double adj_open = 0, adj_close = 0, adj_high = 0, adj_low = 0;  // 复权
        int64_t volume = 0;
        double turnover = 0;
        uint8_t ext = 0;
    };
    std::vector<MergedRow> merged;
    merged.reserve(std::max(org_rows.size(), hfq_rows.size()));

    for (const auto& [dt, r] : org_rows) {
        MergedRow m;
        m.datetime = dt;
        m.open = r.open; m.close = r.close; m.high = r.high; m.low = r.low;
        m.volume = r.volume; m.turnover = r.turnover; m.ext = r.ext;
        auto it = hfq_rows.find(dt);
        if (it != hfq_rows.end()) {
            m.adj_open = it->second.open;
            m.adj_close = it->second.close;
            m.adj_high = it->second.high;
            m.adj_low = it->second.low;
        }
        merged.push_back(std::move(m));
    }
    // hfq 独有（org 缺失）的行：原始列 0，adj 列取 hfq
    for (const auto& [dt, r] : hfq_rows) {
        if (!org_rows.count(dt)) {
            MergedRow m;
            m.datetime = dt;
            m.adj_open = r.open; m.adj_close = r.close;
            m.adj_high = r.high; m.adj_low = r.low;
            m.volume = r.volume; m.turnover = r.turnover; m.ext = r.ext;
            merged.push_back(std::move(m));
        }
    }

    // ── 3. 冲突行预编译 UPDATE + 新行 Appender 插入（同一事务）──
    //    不用 DELETE：Appender 的索引更新是缓冲的（buffered replay），下一次
    //    DELETE 的表扫描触发 ApplyBufferedReplays 回放，与 DELETE 交织会触发
    //    DuckDB ART 索引损坏（BoundIndex::Delete 错误路径格式化 chunk 时卡死）
    if (merged.empty()) {
        SPDLOG_WARN("[QuoteDB] merged empty for {} ({}), skip", table, symbol_str);
        return 0;
    }

    exec_unsafe("BEGIN TRANSACTION");
    auto t_begin = std::chrono::high_resolution_clock::now();

    std::string min_dt = merged.front().datetime;
    std::string max_dt = merged.front().datetime;
    for (const auto& m : merged) {
        if (m.datetime < min_dt) min_dt = m.datetime;
        if (m.datetime > max_dt) max_dt = m.datetime;
    }

    // 已有 datetime 集合（限制在本次数据范围内，走 (symbol, datetime) 索引）
    // CAST(datetime AS VARCHAR) 与 appender 写入的 naive 字符串恒等
    UnorderedSet<std::string> existing;
    {
        duckdb_result sel;
        std::string sql = fmt::format(
            "SELECT CAST(datetime AS VARCHAR) FROM {} WHERE symbol = {} AND datetime BETWEEN '{}' AND '{}'",
            table, sym_encoded, min_dt, max_dt);
        if (duckdb_query(conn(), sql.c_str(), &sel) != DuckDBSuccess) {
            const char* err = duckdb_result_error(&sel);
            SPDLOG_ERROR("[QuoteDB] select existing datetime failed: {}", err ? err : "unknown");
            duckdb_destroy_result(&sel);
            exec_unsafe("ROLLBACK");
            return -1;
        }
        idx_t rows = duckdb_row_count(&sel);
        for (idx_t i = 0; i < rows; ++i) {
            char* s = duckdb_value_varchar(&sel, 0, i);
            if (s) {
                existing.insert(s);
                duckdb_free(s);
            }
        }
        duckdb_destroy_result(&sel);
    }

    // 冲突行：预编译 UPDATE（prepare 一次逐行 bind，UNIQUE(symbol,datetime) 索引定位）
    int updated = 0;
    {
        duckdb_prepared_statement upd = nullptr;
        std::string upd_sql = fmt::format(
            "UPDATE {} SET open=?, close=?, high=?, low=?, volume=?, turnover=?, ext=?, "
            "adj_open=?, adj_close=?, adj_high=?, adj_low=? WHERE symbol=? AND datetime=?",
            table);
        if (duckdb_prepare(conn(), upd_sql.c_str(), &upd) != DuckDBSuccess) {
            const char* err = upd ? duckdb_prepare_error(upd) : "unknown";
            SPDLOG_ERROR("[QuoteDB] prepare update failed: {}", err ? err : "unknown");
            if (upd) duckdb_destroy_prepare(&upd);
            exec_unsafe("ROLLBACK");
            return -1;
        }

        for (const auto& m : merged) {
            if (!existing.count(m.datetime)) continue;
            duckdb_state st = DuckDBSuccess;
            st = duckdb_bind_double(upd, 1, m.open);
            if (st == DuckDBSuccess) st = duckdb_bind_double(upd, 2, m.close);
            if (st == DuckDBSuccess) st = duckdb_bind_double(upd, 3, m.high);
            if (st == DuckDBSuccess) st = duckdb_bind_double(upd, 4, m.low);
            if (st == DuckDBSuccess) st = duckdb_bind_int64(upd, 5, m.volume);
            if (st == DuckDBSuccess) st = duckdb_bind_double(upd, 6, m.turnover);
            if (st == DuckDBSuccess) st = duckdb_bind_int64(upd, 7, m.ext);
            if (st == DuckDBSuccess) st = duckdb_bind_double(upd, 8, m.adj_open);
            if (st == DuckDBSuccess) st = duckdb_bind_double(upd, 9, m.adj_close);
            if (st == DuckDBSuccess) st = duckdb_bind_double(upd, 10, m.adj_high);
            if (st == DuckDBSuccess) st = duckdb_bind_double(upd, 11, m.adj_low);
            if (st == DuckDBSuccess) st = duckdb_bind_int64(upd, 12, sym_encoded);
            if (st == DuckDBSuccess) st = duckdb_bind_timestamp(upd, 13, duckdb_timestamp{FromNaiveTimestamp(m.datetime)});

            if (st != DuckDBSuccess) {
                SPDLOG_ERROR("[QuoteDB] bind update failed at {}", m.datetime);
                duckdb_destroy_prepare(&upd);
                exec_unsafe("ROLLBACK");
                return -1;
            }

            duckdb_result upd_res;
            st = duckdb_execute_prepared(upd, &upd_res);
            bool ok = (st == DuckDBSuccess);
            if (!ok) {
                const char* err = duckdb_result_error(&upd_res);
                SPDLOG_ERROR("[QuoteDB] update failed at {}: {}", m.datetime, err ? err : "unknown");
            }
            duckdb_destroy_result(&upd_res);
            if (!ok) {
                duckdb_destroy_prepare(&upd);
                exec_unsafe("ROLLBACK");
                return -1;
            }
            ++updated;
        }
        duckdb_destroy_prepare(&upd);
    }

    duckdb_appender appender = nullptr;
    if (duckdb_appender_create(conn(), nullptr, table.c_str(), &appender) != DuckDBSuccess) {
        const char* err = duckdb_appender_error(appender);
        SPDLOG_ERROR("[QuoteDB] appender create failed: {}", err ? err : "unknown");
        duckdb_appender_destroy(&appender);
        exec_unsafe("ROLLBACK");
        return -1;
    }

    bool append_ok = true;
    int inserted = 0;
    for (const auto& m : merged) {
        if (existing.count(m.datetime)) continue;  // 冲突行已走 UPDATE
        // 直接以 VARCHAR 写入 datetime 列：DuckDB 在 flush 时按 naive timestamp
        // 解析（无时区转换），保证 input "YYYY-MM-DD HH:MM:SS" → stored → output 恒等
        duckdb_state st = duckdb_append_null(appender);                    // id
        if (st == DuckDBSuccess) st = duckdb_append_int64(appender, sym_encoded);   // symbol
        if (st == DuckDBSuccess) st = duckdb_append_varchar(appender, m.datetime.c_str());  // datetime
        if (st == DuckDBSuccess) st = duckdb_append_double(appender, m.open);       // open
        if (st == DuckDBSuccess) st = duckdb_append_double(appender, m.close);      // close
        if (st == DuckDBSuccess) st = duckdb_append_double(appender, m.high);       // high
        if (st == DuckDBSuccess) st = duckdb_append_double(appender, m.low);        // low
        if (st == DuckDBSuccess) st = duckdb_append_int64(appender, m.volume);      // volume
        if (st == DuckDBSuccess) st = duckdb_append_double(appender, m.turnover);   // turnover
        if (st == DuckDBSuccess) st = duckdb_append_int64(appender, m.ext);         // ext
        if (st == DuckDBSuccess) st = duckdb_append_double(appender, m.adj_open);   // adj_open
        if (st == DuckDBSuccess) st = duckdb_append_double(appender, m.adj_close);  // adj_close
        if (st == DuckDBSuccess) st = duckdb_append_double(appender, m.adj_high);   // adj_high
        if (st == DuckDBSuccess) st = duckdb_append_double(appender, m.adj_low);    // adj_low
        if (st == DuckDBSuccess) st = duckdb_appender_end_row(appender);

        if (st != DuckDBSuccess) {
            const char* err = duckdb_appender_error(appender);
            SPDLOG_ERROR("[QuoteDB] appender append failed at {}: {}", m.datetime, err ? err : "unknown");
            append_ok = false;
            break;
        }
        ++inserted;
    }

    if (append_ok) {
        duckdb_state st = duckdb_appender_flush(appender);
        if (st != DuckDBSuccess) {
            const char* err = duckdb_appender_error(appender);
            SPDLOG_ERROR("[QuoteDB] appender flush failed: {}", err ? err : "unknown");
            append_ok = false;
        }
    }

    duckdb_appender_destroy(&appender);

    if (!append_ok) {
        exec_unsafe("ROLLBACK");
        return -1;
    }

    exec_unsafe("COMMIT");

    auto t_commit = std::chrono::high_resolution_clock::now();
    auto commit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_commit - t_begin).count();
    SPDLOG_INFO("[QuoteDB] Transaction: {} updated + {} inserted in {}ms (BEGIN to COMMIT)", updated, inserted, commit_ms);

    auto t_end = std::chrono::high_resolution_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    int total = updated + inserted;
    SPDLOG_INFO("[QuoteDB] Imported {} rows ({}+{}) into {} for {} in {}ms total",
                total, updated, inserted, table, symbol_str, total_ms);
    return total;
}

// ═══════════════════════════════════════════════════════════
//  插入或更新单根 bar
// ═══════════════════════════════════════════════════════════

bool QuoteDB::upsertBar(const std::string& table, const QuoteBar& bar) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return false;

    ensureTable(table);

    int64_t sym_encoded = encodeSymbol(bar.symbol);

    // adj_* 为 0 时用原始价格填充（确保复权价格始终有效）
    double adj_open  = bar.adj_open  > 0 ? bar.adj_open  : bar.open;
    double adj_close = bar.adj_close > 0 ? bar.adj_close : bar.close;
    double adj_high  = bar.adj_high  > 0 ? bar.adj_high  : bar.high;
    double adj_low   = bar.adj_low   > 0 ? bar.adj_low   : bar.low;

    std::string sql = fmt::format(
        "INSERT INTO {} (symbol, datetime, open, close, high, low, volume, turnover, ext, "
        "adj_open, adj_close, adj_high, adj_low) "
        "VALUES ({}, '{}', {:.6f}, {:.6f}, {:.6f}, {:.6f}, {}, {:.4f}, {}, "
        "{:.6f}, {:.6f}, {:.6f}, {:.6f}) "
        "ON CONFLICT(symbol, datetime) DO UPDATE SET "
        "open=excluded.open, close=excluded.close, high=excluded.high, low=excluded.low, "
        "volume=excluded.volume, turnover=excluded.turnover, ext=excluded.ext, "
        "adj_open=excluded.adj_open, adj_close=excluded.adj_close, "
        "adj_high=excluded.adj_high, adj_low=excluded.adj_low",
        table, sym_encoded, bar.datetime,
        bar.open, bar.close, bar.high, bar.low,
        bar.volume, bar.turnover, (int)bar.ext,
        adj_open, adj_close, adj_high, adj_low);

    if (!exec(sql)) {
        SPDLOG_ERROR("[QuoteDB] upsertBar failed for {} at {}", bar.symbol, bar.datetime);
        return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════
//  查询
// ═══════════════════════════════════════════════════════════

std::vector<QuoteBar> QuoteDB::query(const std::string& table,
                                     const std::string& symbol,
                                     const std::string& start_time,
                                     const std::string& end_time,
                                     int limit) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    std::vector<QuoteBar> result;
    if (!isInitialized()) return result;

    // 检查表是否存在（quote.db 刚创建时表尚未建立）
    auto tables = listTables();
    if (std::find(tables.begin(), tables.end(), table) == tables.end()) {
        SPDLOG_WARN("[QuoteDB] query: table '{}' not found in main schema (available tables: {})",
                     table, tables.empty() ? "<none>" : fmt::format("{}", fmt::join(tables, ", ")));
        return result;
    }

    int64_t sym_encoded = encodeSymbol(symbol);

    std::string sql = fmt::format(
        "SELECT symbol, CAST(datetime AS VARCHAR), open, close, high, low, volume, turnover, ext, "
        "adj_open, adj_close, adj_high, adj_low "
        "FROM {} WHERE symbol = {}", table, sym_encoded);

    if (!start_time.empty())
        sql += fmt::format(" AND datetime >= '{}'", start_time);
    if (!end_time.empty())
        sql += fmt::format(" AND datetime <= '{}'", end_time);

    sql += " ORDER BY datetime ASC";
    if (limit > 0)
        sql += fmt::format(" LIMIT {}", limit);

    duckdb_result res;
    if (duckdb_query(conn(), sql.c_str(), &res) != DuckDBSuccess) {
        const char* err = duckdb_result_error(&res);
        SPDLOG_ERROR("[QuoteDB] Query failed: {} (SQL: {})", err ? err : "unknown",
                     sql.size() > 200 ? sql.substr(0, 200) + "..." : sql);
        duckdb_destroy_result(&res);
        return result;
    }

    idx_t row_count = duckdb_row_count(&res);
    if (row_count == 0) {
        SPDLOG_WARN("[QuoteDB] query: table '{}' exists but returned 0 rows for symbol='{}' (encoded={})",
                     table, symbol, sym_encoded);
    }
    for (idx_t i = 0; i < row_count; i++) {
        QuoteBar bar;
        bar.symbol   = symbol;
        bar.datetime = duckdb_value_varchar(&res, 1, i);
        bar.open     = duckdb_value_double(&res, 2, i);
        bar.close    = duckdb_value_double(&res, 3, i);
        bar.high     = duckdb_value_double(&res, 4, i);
        bar.low      = duckdb_value_double(&res, 5, i);
        bar.volume   = duckdb_value_int64(&res, 6, i);
        bar.turnover = duckdb_value_double(&res, 7, i);
        bar.ext      = static_cast<uint8_t>(duckdb_value_int8(&res, 8, i));
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
    std::lock_guard<std::recursive_mutex> lock(mtx());
    std::vector<std::string> tables;
    if (!isInitialized()) return tables;

    DuckDBBaseT<QuoteDB>::query("SELECT table_name FROM information_schema.tables WHERE table_schema='main' ORDER BY table_name",
          [&](duckdb_result& res) -> bool {
              idx_t rows = duckdb_row_count(&res);
              for (idx_t i = 0; i < rows; i++) {
                  tables.push_back(duckdb_value_varchar(&res, 0, i));
              }
              return true;
          });
    return tables;
}

std::vector<std::string> QuoteDB::listSymbols(const std::string& table) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    std::vector<std::string> symbols;
    if (!isInitialized()) return symbols;

    std::string sql = fmt::format("SELECT DISTINCT symbol FROM {}", table);
    duckdb_result res;
    if (duckdb_query(conn(), sql.c_str(), &res) == DuckDBSuccess) {
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
//  删除表 / 标的
// ═══════════════════════════════════════════════════════════

bool QuoteDB::dropTable(const std::string& table) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    std::string sql = fmt::format("DROP TABLE IF EXISTS {}", table);
    return exec(sql);
}

bool QuoteDB::deleteSymbol(const std::string& table, const std::string& symbol) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
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

bool QuoteDB::deleteBar(const std::string& table, const std::string& symbol, const std::string& datetime) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    int64_t sym_encoded = encodeSymbol(symbol);
    // datetime 入参可能是 "YYYY-MM-DD" 或 "YYYY-MM-DD HH:MM:SS"；datetime 列通常为 TIMESTAMP
    // 用 prefix 匹配保证秒级以下精度差异也能命中
    std::string sql = fmt::format(
        "DELETE FROM {} WHERE symbol = {} AND CAST(datetime AS VARCHAR) LIKE '{}%'",
        table, sym_encoded, datetime);
    bool ok = exec(sql);
    if (!ok) {
        SPDLOG_ERROR("[QuoteDB] Failed to delete bar {}@{} from {}", datetime, symbol, table);
    } else {
        SPDLOG_INFO("[QuoteDB] Deleted bar {}@{} from {}", datetime, symbol, table);
    }
    return ok;
}

std::vector<QuoteDB::SymbolTimeRange> QuoteDB::getSymbolTimeRanges(const std::string& table) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    std::vector<SymbolTimeRange> result;
    if (!isInitialized()) return result;

    std::string sql = fmt::format(
        "SELECT symbol, MIN(datetime), MAX(datetime), COUNT(*) FROM {} GROUP BY symbol ORDER BY symbol",
        table);

    duckdb_result db_result;
    if (duckdb_query(conn(), sql.c_str(), &db_result) != DuckDBSuccess) {
        duckdb_destroy_result(&db_result);
        return result;
    }

    idx_t rows = duckdb_row_count(&db_result);
    for (idx_t i = 0; i < rows; i++) {
        SymbolTimeRange range;
        int64_t encoded = duckdb_value_int64(&db_result, 0, i);
        std::memcpy(&range.symbol, &encoded, sizeof(symbol_t));
        range.start_time = duckdb_value_varchar(&db_result, 1, i);
        range.end_time = duckdb_value_varchar(&db_result, 2, i);
        range.count = static_cast<int64_t>(duckdb_value_int64(&db_result, 3, i));
        result.push_back(std::move(range));
    }

    duckdb_destroy_result(&db_result);
    return result;
}

int QuoteDB::updateAdjPrices(const std::string& table, int64_t encoded_symbol,
                             const std::vector<AdjPriceUpdate>& updates) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return -1;

    exec_unsafe("BEGIN TRANSACTION");
    int updated = 0;
    for (auto& u : updates) {
        String sql = fmt::format(
            "UPDATE {} SET adj_open={:.6f}, adj_close={:.6f}, adj_high={:.6f}, adj_low={:.6f} "
            "WHERE symbol={} AND datetime='{}'",
            table, u.adj_open, u.adj_close, u.adj_high, u.adj_low,
            encoded_symbol, u.datetime);
        if (exec(sql)) ++updated;
    }
    exec_unsafe("COMMIT");

    SPDLOG_INFO("[QuoteDB] updateAdjPrices: {} rows updated for symbol={}", updated, encoded_symbol);
    return updated;
}

double QuoteDB::getLatestClose(const std::string& table, const std::string& symbol_str) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return 0.0;

    int64_t encoded = encodeSymbol(symbol_str);
    std::string sql = fmt::format(
        "SELECT close FROM {} WHERE symbol = {} ORDER BY datetime DESC LIMIT 1",
        table, encoded);

    duckdb_result result;
    if (duckdb_query(conn(), sql.c_str(), &result) != DuckDBSuccess) {
        duckdb_destroy_result(&result);
        return 0.0;
    }

    double close = 0.0;
    if (duckdb_row_count(&result) > 0) {
        close = duckdb_value_double(&result, 0, 0);
    }
    duckdb_destroy_result(&result);
    return close;
}
