#include "Util/DecisionDB.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
// TradeAction 的完整定义（decision_to_action 返回该枚举，此处需与 TradeAction::BUY 比较）
#include "DataContext.h"
#include <cstring>
#include <algorithm>

DecisionDB& DecisionDB::instance() {
    static DecisionDB inst;
    return inst;
}

void DecisionDB::ensureTables() {
    exec_unsafe(R"(
        CREATE TABLE IF NOT EXISTS decisions (
            id          INTEGER PRIMARY KEY,
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
            exec_price  DOUBLE DEFAULT 0,
            closed      BOOLEAN DEFAULT FALSE
        )
    )");
    exec_unsafe("CREATE INDEX IF NOT EXISTS idx_decisions_date ON decisions(timestamp)");

    exec_unsafe(R"(
        CREATE TABLE IF NOT EXISTS daily_positions (
            strategy    VARCHAR NOT NULL,
            symbol      BIGINT NOT NULL,
            date        BIGINT NOT NULL,
            position    BIGINT NOT NULL DEFAULT 0,
            close_price DOUBLE NOT NULL DEFAULT 0,
            UNIQUE(strategy, symbol, date)
        )
    )");
    exec_unsafe("CREATE INDEX IF NOT EXISTS idx_dp_strategy ON daily_positions(strategy, date)");
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
    // 本地时区格式化：_timestamp 由 FromStr("YYYY-MM-DD") 得到本地零点（与
    // daily_positions / 报告目录同一口径），而 queryByDate 直接拿
    // CAST(timestamp AS DATE) 和调用方的日期字符串比较。此前用 gmtime，在 UTC+8
    // 会把本地零点格式化成前一天 16:00，按归属日查询整整差一天。
#ifdef _WIN32
    localtime_s(&tm_val, &ts);
#else
    localtime_r(&ts, &tm_val);
#endif
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_val);

    // 策略名直接拼进 SQL 字面量：转义单引号。名字里带引号时整条 INSERT 会失败，
    // 决策就静默丢了（面板看不到 → 没法下单）。
    std::string sql = fmt::format(
        "INSERT OR REPLACE INTO decisions (id, strategy, symbol, action, is_open, quantity, price, epoch, timestamp, executed, exec_qty, exec_price, closed) "
        "VALUES ({}, '{}', {}, {}, {}, {}, {:.6f}, {}, TIMESTAMP '{}', {}, {}, {:.6f}, {})",
        record._id,
        escapeSqlLiteral(record._strategy),
        sym_encoded,
        static_cast<int>(record._action),
        record._is_open ? 1 : 0,
        record._quantity,
        record._price,
        record._epoch,
        timebuf,
        record._executed ? 1 : 0,
        record._executed_quantity,
        record._executed_price,
        record._closed ? 1 : 0
    );

    if (!exec(sql)) {
        // 重新执行一次以获取具体错误信息
        duckdb_result r;
        auto st = duckdb_query(conn(), sql.c_str(), &r);
        const char* err = (st != DuckDBSuccess) ? duckdb_result_error(&r) : "unknown";
        FATAL("[DecisionDB] insertDecision failed: id={} sql={} err={}", record._id, sql, err ? err : "null");
        duckdb_destroy_result(&r);
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
        "executed, exec_qty, exec_price, closed FROM decisions "
        "WHERE CAST(timestamp AS DATE) = '{}' ORDER BY id",
        escapeSqlLiteral(date)
    );

    query(sql, [&](duckdb_result& result) -> bool {
        int64_t row_count = duckdb_row_count(&result);
        for (int64_t i = 0; i < row_count; ++i) {
            DecisionRecord rec{};
            rec._id = duckdb_value_int32(&result, 0, i);

            const char* strategy = duckdb_value_varchar(&result, 1, i);
            if (strategy) {
                rec._strategy = strategy;
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
            rec._closed = duckdb_value_boolean(&result, 12, i);
            rec._reserved = 0;

            results.push_back(rec);
        }
        return true;
    });

    return results;
}

// ═══════════════════════════════════════════════════════════
//  最新归属日
// ═══════════════════════════════════════════════════════════

// decisions 表按 timestamp 的日期过滤，而 timestamp 是决策的归属日
// （日终回放的模拟交易日），与墙钟日期差一天是常态。
// 查"最新一批决策"必须先问库要最新归属日，不能拿今天当默认。
std::string DecisionDB::queryLatestDate() {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return {};

    std::string date;
    const char* sql =
        "SELECT strftime(MAX(CAST(timestamp AS DATE)), '%Y-%m-%d') FROM decisions";

    query(sql, [&](duckdb_result& result) -> bool {
        if (duckdb_row_count(&result) > 0 && !duckdb_value_is_null(&result, 0, 0)) {
            const char* d = duckdb_value_varchar(&result, 0, 0);
            if (d) {
                date = d;
                duckdb_free((void*)d);
            }
        }
        return true;
    });

    return date;
}

// ═══════════════════════════════════════════════════════════
//  标记已执行
// ═══════════════════════════════════════════════════════════

bool DecisionDB::exists(int id) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return false;

    auto sql = fmt::format("SELECT COUNT(*) FROM decisions WHERE id = {}", id);
    bool found = false;
    query(sql, [&found](duckdb_result& result) {
        if (duckdb_row_count(&result) > 0) {
            auto* val = duckdb_value_varchar(&result, 0, 0);
            if (val) {
                found = std::stoi(val) > 0;
                duckdb_free(val);
            }
        }
        return true;
    });
    return found;
}

bool DecisionDB::markExecuted(int id, int64_t exec_qty, double exec_price) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return false;
    if (!exists(id)) return false;

    std::string sql = fmt::format(
        "UPDATE decisions SET executed = true, exec_qty = {}, exec_price = {:.6f} WHERE id = {}",
        exec_qty, exec_price, id
    );
    return exec(sql);
}

// ═══════════════════════════════════════════════════════════
//  标记已确认（关闭，不执行）
// ═══════════════════════════════════════════════════════════

bool DecisionDB::markClosed(int id) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return false;
    if (!exists(id)) return false;

    std::string sql = fmt::format(
        "UPDATE decisions SET closed = true WHERE id = {}", id
    );
    return exec(sql);
}

int DecisionDB::clearByDate(const std::string& date) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return 0;

    // 先查数量
    auto selectSql = fmt::format(
        "SELECT COUNT(*) FROM decisions WHERE CAST(timestamp AS DATE) = '{}'", escapeSqlLiteral(date)
    );
    int count = 0;
    query(selectSql, [&count](duckdb_result& result) {
        if (duckdb_row_count(&result) > 0) {
            auto* val = duckdb_value_varchar(&result, 0, 0);
            if (val) {
                count = std::stoi(val);
                duckdb_free(val);
            }
        }
        return true;
    });

    if (count > 0) {
        auto deleteSql = fmt::format(
            "DELETE FROM decisions WHERE CAST(timestamp AS DATE) = '{}'", escapeSqlLiteral(date)
        );
        exec(deleteSql);
    }
    return count;
}

// ═══════════════════════════════════════════════════════════
//  日终持仓快照
// ═══════════════════════════════════════════════════════════

void DecisionDB::insertDailyPosition(const DailyPositionRecord& record) {
    insertDailyPositions({record});
}

void DecisionDB::insertDailyPositions(const std::vector<DailyPositionRecord>& records) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized() || records.empty()) return;

    // 单条多行 INSERT + 分块 500 行，两个约束都是踩过坑换来的：
    // 1) 逐行 INSERT 每行一次 duckdb_query，几十行就要几百毫秒（实测 63 行 ≈ 0.69s），
    //    而这段延迟卡在日终回调里，"决策可见到持仓落库"之间凭空多出这段时间。
    // 2) 单条 INSERT 不能超过 ~500 行：再多 DuckDB 的 ART 索引会在 ON CONFLICT
    //    upsert 下损坏（Corrupted ART index），且损坏后不可修复、只能删库重建。
    static constexpr size_t CHUNK_SIZE = 500;

    std::string begin_err = execWithError("BEGIN TRANSACTION");
    if (!begin_err.empty()) {
        WARN("[DecisionDB] insertDailyPositions: BEGIN TRANSACTION failed: {}", begin_err);
        return;
    }

    for (size_t start = 0; start < records.size(); start += CHUNK_SIZE) {
        const size_t end = std::min(start + CHUNK_SIZE, records.size());

        std::string sql;
        sql.reserve((end - start) * 96 + 256);
        sql += "INSERT INTO daily_positions (strategy, symbol, date, position, close_price) VALUES ";
        for (size_t i = start; i < end; ++i) {
            const auto& rec = records[i];
            if (i > start) sql += ", ";
            sql += fmt::format("('{}', {}, {}, {}, {:.6f})",
                               escapeSqlLiteral(rec.strategy),
                               encodeSymbol(rec.symbol),
                               static_cast<int64_t>(rec.date),
                               rec.position,
                               rec.close_price);
        }
        // UPSERT：同日同标的覆盖
        sql += " ON CONFLICT (strategy, symbol, date) DO UPDATE SET "
               "position=excluded.position, close_price=excluded.close_price";

        std::string err = execWithError(sql);
        if (!err.empty()) {
            const auto& bad = records[start];
            WARN("[DecisionDB] insertDailyPositions failed: strategy={}, symbol={}, date={}, "
                 "rows={}/{}, err={}",
                 bad.strategy, get_symbol(bad.symbol), static_cast<int64_t>(bad.date),
                 end - start, records.size(), err);
            exec_unsafe("ROLLBACK");
            return;
        }
    }

    std::string commit_err = execWithError("COMMIT");
    if (!commit_err.empty()) {
        WARN("[DecisionDB] insertDailyPositions: COMMIT failed: {}", commit_err);
        exec_unsafe("ROLLBACK");
    }
}

std::vector<DailyPositionRecord> DecisionDB::queryDailyPositions(
    const std::string& strategy, time_t startDate, time_t endDate) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    std::vector<DailyPositionRecord> results;
    if (!isInitialized()) return results;

    std::string sql = fmt::format(
        "SELECT strategy, symbol, date, position, close_price FROM daily_positions "
        "WHERE strategy = '{}'", escapeSqlLiteral(strategy));

    if (startDate > 0)
        sql += fmt::format(" AND date >= {}", static_cast<int64_t>(startDate));
    if (endDate > 0)
        sql += fmt::format(" AND date <= {}", static_cast<int64_t>(endDate));

    sql += " ORDER BY date ASC, symbol ASC";

    query(sql, [&](duckdb_result& result) -> bool {
        int64_t row_count = duckdb_row_count(&result);
        for (int64_t i = 0; i < row_count; ++i) {
            DailyPositionRecord rec;
            const char* strat = duckdb_value_varchar(&result, 0, i);
            if (strat) {
                rec.strategy = strat;
                duckdb_free((void*)strat);
            }
            int64_t sym_encoded = duckdb_value_int64(&result, 1, i);
            rec.symbol = decodeSymbol(sym_encoded);
            rec.date = static_cast<time_t>(duckdb_value_int64(&result, 2, i));
            rec.position = duckdb_value_int64(&result, 3, i);
            rec.close_price = duckdb_value_double(&result, 4, i);
            results.push_back(rec);
        }
        return true;
    });

    return results;
}

bool DecisionDB::deleteDailyPosition(const std::string& strategy, symbol_t symbol, time_t date) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return false;

    int64_t sym_encoded = encodeSymbol(symbol);
    int64_t date_ts = static_cast<int64_t>(date);

    std::string sql = fmt::format(
        "DELETE FROM daily_positions WHERE strategy = '{}' AND symbol = {} AND date = {}",
        escapeSqlLiteral(strategy), sym_encoded, date_ts
    );

    if (!exec(sql)) {
        WARN("[DecisionDB] deleteDailyPosition failed: strategy={}, symbol={}, date={}",
             strategy, get_symbol(symbol), date_ts);
        return false;
    }
    return true;
}

bool DecisionDB::queryDecisionInfo(int id, std::string& strategy, symbol_t& symbol, time_t& timestamp) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return false;

    std::string sql = fmt::format(
        "SELECT strategy, symbol, epoch(timestamp) FROM decisions WHERE id = {}", id
    );

    bool found = false;
    query(sql, [&](duckdb_result& result) -> bool {
        int64_t row_count = duckdb_row_count(&result);
        if (row_count > 0) {
            const char* strat = duckdb_value_varchar(&result, 0, 0);
            if (strat) {
                strategy = strat;
                duckdb_free((void*)strat);
            }
            int64_t sym_encoded = duckdb_value_int64(&result, 1, 0);
            symbol = decodeSymbol(sym_encoded);
            timestamp = static_cast<time_t>(duckdb_value_int64(&result, 2, 0));
            found = true;
        }
        return true;
    });

    return found;
}

// ═══════════════════════════════════════════════════════════
//  成交流水查询（持仓恢复用）
// ═══════════════════════════════════════════════════════════

List<TradeInfo> DecisionDB::queryExecutedFills(const std::string& strategy) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    List<TradeInfo> results;
    if (!isInitialized()) return results;

    // 只取实际成交（executed）且未被关闭（closed）的决策。
    // exec_qty 为 0 说明尚未成交或成交数量丢失，跳过。
    // 同秒多笔按 id 兜底排序，保证 FIFO 顺序稳定。
    std::string sql = fmt::format(
        "SELECT symbol, action, exec_qty, exec_price, epoch(timestamp) AS ts FROM decisions "
        "WHERE strategy = '{}' AND executed = true AND closed = false AND exec_qty > 0 "
        "ORDER BY symbol ASC, ts ASC, id ASC",
        escapeSqlLiteral(strategy));

    query(sql, [&](duckdb_result& result) -> bool {
        int64_t row_count = duckdb_row_count(&result);
        for (int64_t i = 0; i < row_count; ++i) {
            int64_t sym_encoded = duckdb_value_int64(&result, 0, i);
            symbol_t sym = decodeSymbol(sym_encoded);
            auto action = static_cast<DecisionAction>(duckdb_value_uint8(&result, 1, i));

            TradeReport report{};
            report._status = OrderStatus::OrderSuccess;
            report._quantity = static_cast<int>(duckdb_value_int64(&result, 2, i));
            report._price = duckdb_value_double(&result, 3, i);
            report._time = static_cast<time_t>(duckdb_value_int64(&result, 4, i));
            // side: 0=买 / 1=卖（与 BrokerSubSystem.cpp:1260 一致）
            report._side = (decision_to_action(action) == TradeAction::BUY) ? 0 : 1;
            // flag: 0=开仓 / 1=平仓
            report._flag = static_cast<char>(decision_to_flag(action));

            // 按标的分组（SQL 已按 symbol 排序，同标的行连续）
            if (results.empty() || results.back()._symbol != sym) {
                TradeInfo info{};
                info._symbol = sym;
                results.push_back(info);
            }
            results.back()._reports.push_back(report);
        }
        return true;
    });

    return results;
}

List<String> DecisionDB::queryFilledStrategies() {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    List<String> results;
    if (!isInitialized()) return results;

    const char* sql =
        "SELECT DISTINCT strategy FROM decisions "
        "WHERE executed = true AND exec_qty > 0 ORDER BY strategy";

    query(sql, [&](duckdb_result& result) -> bool {
        int64_t row_count = duckdb_row_count(&result);
        for (int64_t i = 0; i < row_count; ++i) {
            const char* strat = duckdb_value_varchar(&result, 0, i);
            if (strat) {
                results.push_back(String(strat));
                duckdb_free((void*)strat);
            }
        }
        return true;
    });

    return results;
}

int64_t DecisionDB::maxDecisionId() {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return 0;

    int64_t maxId = 0;
    query("SELECT COALESCE(MAX(id), 0) FROM decisions", [&](duckdb_result& result) -> bool {
        if (duckdb_row_count(&result) > 0 && !duckdb_value_is_null(&result, 0, 0)) {
            maxId = duckdb_value_int64(&result, 0, 0);
        }
        return true;
    });
    return maxId;
}

// ═══════════════════════════════════════════════════════════
//  删除指定策略的所有历史记录
// ═══════════════════════════════════════════════════════════

int DecisionDB::deleteByStrategy(const std::string& strategy) {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    if (!isInitialized()) return 0;

    int totalDeleted = 0;

    // 删除 decisions 表中该策略的记录
    {
        auto countSql = fmt::format(
            "SELECT COUNT(*) FROM decisions WHERE strategy = '{}'", escapeSqlLiteral(strategy));
        int count = 0;
        query(countSql, [&count](duckdb_result& result) {
            if (duckdb_row_count(&result) > 0) {
                auto* val = duckdb_value_varchar(&result, 0, 0);
                if (val) {
                    count = std::stoi(val);
                    duckdb_free(val);
                }
            }
            return true;
        });

        if (count > 0) {
            auto deleteSql = fmt::format(
                "DELETE FROM decisions WHERE strategy = '{}'", escapeSqlLiteral(strategy));
            exec(deleteSql);
            totalDeleted += count;
        }
    }

    // 删除 daily_positions 表中该策略的记录
    {
        auto countSql = fmt::format(
            "SELECT COUNT(*) FROM daily_positions WHERE strategy = '{}'", escapeSqlLiteral(strategy));
        int count = 0;
        query(countSql, [&count](duckdb_result& result) {
            if (duckdb_row_count(&result) > 0) {
                auto* val = duckdb_value_varchar(&result, 0, 0);
                if (val) {
                    count = std::stoi(val);
                    duckdb_free(val);
                }
            }
            return true;
        });

        if (count > 0) {
            auto deleteSql = fmt::format(
                "DELETE FROM daily_positions WHERE strategy = '{}'", escapeSqlLiteral(strategy));
            exec(deleteSql);
            totalDeleted += count;
        }
    }

    if (totalDeleted > 0) {
        exec("VACUUM");
    }

    return totalDeleted;
}
