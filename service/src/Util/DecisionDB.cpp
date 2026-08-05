#include "Util/DecisionDB.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Util/log.h"
#include <cstring>

DecisionDB& DecisionDB::instance() {
    static DecisionDB inst;
    return inst;
}

void DecisionDB::ensureTables() {
    exec_unsafe(R"(
        CREATE TABLE IF NOT EXISTS decisions (
            id          INTEGER,
            strategy    VARCHAR,
            symbol      BIGINT NOT NULL,
            action      TINYINT NOT NULL,
            is_open     BOOLEAN NOT NULL,
            quantity    BIGINT NOT NULL,
            price       DOUBLE NOT NULL,
            epoch       INTEGER,
            timestamp   TIMESTAMP NOT NULL,
            executed    BOOLEAN DEFAULT FALSE,
            exec_qty    BIGINT DEFAULT 0,
            exec_price  DOUBLE DEFAULT 0
        )
    )");
    exec_unsafe("CREATE INDEX IF NOT EXISTS idx_decisions_date ON decisions(timestamp)");
    exec_unsafe("CREATE INDEX IF NOT EXISTS idx_decisions_id ON decisions(id)");
}

// ═══════════════════════════════════════════════════════════
//  symbol 编解码
// ═══════════════════════════════════════════════════════════

static int64_t encodeSymbol(symbol_t sym) {
    int64_t val = 0;
    std::memcpy(&val, &sym, sizeof(symbol_t));
    return val;
}

static symbol_t decodeSymbol(int64_t encoded) {
    symbol_t s;
    std::memcpy(&s, &encoded, sizeof(symbol_t));
    return s;
}

// ═══════════════════════════════════════════════════════════
//  写入决策
// ═══════════════════════════════════════════════════════════

int DecisionDB::insertDecision(const DecisionRecord& record) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return -1;

    int64_t sym_encoded = encodeSymbol(record._symbol);
    char timebuf[64];
    struct tm tm_val;
    time_t ts = record._timestamp;
    gmtime_r(&ts, &tm_val);
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_val);

    std::string sql = fmt::format(
        "INSERT INTO decisions (id, strategy, symbol, action, is_open, quantity, price, epoch, timestamp, executed, exec_qty, exec_price) "
        "VALUES ({}, '{}', {}, {}, {}, {}, {:.6f}, {}, TIMESTAMP '{}', {}, {}, {:.6f})",
        record._id,
        record._strategy,
        sym_encoded,
        static_cast<int>(record._action),
        record._is_open ? 1 : 0,
        record._quantity,
        record._price,
        record._epoch,
        timebuf,
        record._executed ? 1 : 0,
        record._executed_quantity,
        record._executed_price
    );

    if (!exec(sql)) {
        FATAL("[DecisionDB] insertDecision failed: id={}", record._id);
        return -1;
    }
    return record._id;
}

// ═══════════════════════════════════════════════════════════
//  按日期查询
// ═══════════════════════════════════════════════════════════

std::vector<DecisionRecord> DecisionDB::queryByDate(const std::string& date) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    std::vector<DecisionRecord> results;
    if (!isInitialized()) return results;

    std::string sql = fmt::format(
        "SELECT id, strategy, symbol, action, is_open, quantity, price, epoch, epoch(timestamp) as ts, "
        "executed, exec_qty, exec_price FROM decisions "
        "WHERE CAST(timestamp AS DATE) = '{}' ORDER BY id",
        date
    );

    query(sql, [&](duckdb_result& result) -> bool {
        int64_t row_count = duckdb_row_count(&result);
        for (int64_t i = 0; i < row_count; ++i) {
            DecisionRecord rec{};
            rec._id = duckdb_value_int32(&result, 0, i);

            const char* strategy = duckdb_value_varchar(&result, 1, i);
            if (strategy) {
                std::strncpy(rec._strategy, strategy, sizeof(rec._strategy) - 1);
                rec._strategy[sizeof(rec._strategy) - 1] = '\0';
                duckdb_free((void*)strategy);
            }

            int64_t sym_encoded = duckdb_value_int64(&result, 2, i);
            rec._symbol = decodeSymbol(sym_encoded);

            rec._action = static_cast<DecisionAction>(duckdb_value_uint8(&result, 3, i));
            rec._is_open = duckdb_value_boolean(&result, 4, i);
            rec._quantity = duckdb_value_int64(&result, 5, i);
            rec._price = duckdb_value_double(&result, 6, i);
            rec._epoch = duckdb_value_int32(&result, 7, i);
            rec._timestamp = static_cast<time_t>(duckdb_value_int64(&result, 8, i));
            rec._executed = duckdb_value_boolean(&result, 9, i);
            rec._executed_quantity = duckdb_value_int64(&result, 10, i);
            rec._executed_price = duckdb_value_double(&result, 11, i);
            rec._reserved = 0;

            results.push_back(rec);
        }
        return true;
    });

    return results;
}

// ═══════════════════════════════════════════════════════════
//  标记已执行
// ═══════════════════════════════════════════════════════════

bool DecisionDB::markExecuted(int id, int64_t exec_qty, double exec_price) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return false;

    std::string sql = fmt::format(
        "UPDATE decisions SET executed = true, exec_qty = {}, exec_price = {:.6f} WHERE id = {}",
        exec_qty, exec_price, id
    );
    return exec(sql);
}
