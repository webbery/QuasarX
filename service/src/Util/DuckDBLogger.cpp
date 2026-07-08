#include "Util/DuckDBLogger.h"
#include "Util/datetime.h"
#include "Util/string_algorithm.h"
#include <chrono>
#include <algorithm>
#include <filesystem>

// ──────────────────────────────────────────────────────────────────────
// 辅助：创建 duckdb_value（新版不透明指针 API）
// ──────────────────────────────────────────────────────────────────────

duckdb_value DuckDBLogger::make_varchar(const std::string& str) {
    // Windows 下可能是 GBK 编码，需要转为 UTF-8 再存入 DuckDB
#ifdef _WIN32
    std::string utf8_str = to_utf8(str);
    return duckdb_create_varchar(utf8_str.empty() ? str.c_str() : utf8_str.c_str());
#else
    return duckdb_create_varchar(str.c_str());
#endif
}

duckdb_value DuckDBLogger::make_int32(int32_t v) {
    return duckdb_create_int32(v);
}

duckdb_value DuckDBLogger::make_int64(int64_t v) {
    return duckdb_create_int64(v);
}

duckdb_value DuckDBLogger::make_null() {
    return duckdb_create_null_value();
}

bool DuckDBLogger::bind_value_at(duckdb_prepared_statement stmt, idx_t index, duckdb_value val) {
    return duckdb_bind_value(stmt, index, val) == DuckDBSuccess;
}

// ──────────────────────────────────────────────────────────────────────
// 实例 & 销毁
// ──────────────────────────────────────────────────────────────────────

DuckDBLogger& DuckDBLogger::instance() {
    static DuckDBLogger instance;
    return instance;
}

DuckDBLogger::~DuckDBLogger() {
    shutdown();
}

// ──────────────────────────────────────────────────────────────────────
// 低层执行函数
// ──────────────────────────────────────────────────────────────────────

bool DuckDBLogger::exec(const String& sql) {
    duckdb_result result;
    duckdb_state state = duckdb_query(conn_, sql.c_str(), &result);
    bool ok = (state == DuckDBSuccess);
    if (!ok) {
        const char* err = duckdb_result_error(&result);
        SPDLOG_ERROR("[DuckDBLogger] Exec failed: {} [{}]", err ? err : "unknown", sql);
    }
    duckdb_destroy_result(&result);
    return ok;
}

bool DuckDBLogger::table_exists(const String& name) {
    duckdb_result result;
    std::string sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='" + name + "'";
    bool exists = false;
    if (duckdb_query(conn_, sql.c_str(), &result) == DuckDBSuccess) {
        if (duckdb_row_count(&result) > 0) {
            auto* data = (int64_t*)duckdb_column_data(&result, 0);
            auto* nulls = duckdb_nullmask_data(&result, 0);
            if (data && !nulls[0]) exists = data[0] > 0;
        }
    }
    duckdb_destroy_result(&result);
    return exists;
}

bool DuckDBLogger::exec_params(const String& sql, const Vector<duckdb_value>& params) {
    duckdb_prepared_statement stmt = nullptr;
    duckdb_state state = duckdb_prepare(conn_, sql.c_str(), &stmt);
    if (state != DuckDBSuccess) {
        const char* err = stmt ? duckdb_prepare_error(stmt) : "unknown";
        SPDLOG_ERROR("[DuckDBLogger] Prepare failed: {} [{}]", err ? err : "unknown", sql);
        if (stmt) duckdb_destroy_prepare(&stmt);
        return false;
    }

    for (idx_t i = 0; i < params.size(); ++i) {
        if (!bind_value_at(stmt, i + 1, params[i])) {
            SPDLOG_ERROR("[DuckDBLogger] Bind param {} failed", i + 1);
            duckdb_destroy_prepare(&stmt);
            return false;
        }
    }

    duckdb_result result;
    state = duckdb_execute_prepared(stmt, &result);
    bool ok = (state == DuckDBSuccess);
    if (!ok) {
        const char* err = duckdb_result_error(&result);
        SPDLOG_ERROR("[DuckDBLogger] Execute failed: {} [{}]", err ? err : "unknown", sql);
    }
    duckdb_destroy_result(&result);
    duckdb_destroy_prepare(&stmt);
    return ok;
}

bool DuckDBLogger::query_params(const String& sql,
                                const Vector<duckdb_value>& params,
                                duckdb_result& out_result) {
    duckdb_prepared_statement stmt = nullptr;
    duckdb_state state = duckdb_prepare(conn_, sql.c_str(), &stmt);
    if (state != DuckDBSuccess) {
        const char* err = stmt ? duckdb_prepare_error(stmt) : "unknown";
        SPDLOG_ERROR("[DuckDBLogger] Prepare failed: {} [{}]", err ? err : "unknown", sql);
        if (stmt) duckdb_destroy_prepare(&stmt);
        return false;
    }

    for (idx_t i = 0; i < params.size(); ++i) {
        if (!bind_value_at(stmt, i + 1, params[i])) {
            SPDLOG_ERROR("[DuckDBLogger] Bind param {} failed", i + 1);
            duckdb_destroy_prepare(&stmt);
            return false;
        }
    }

    state = duckdb_execute_prepared(stmt, &out_result);
    if (state != DuckDBSuccess) {
        const char* err = duckdb_result_error(&out_result);
        SPDLOG_ERROR("[DuckDBLogger] Query failed: {} [{}]", err ? err : "unknown", sql);
        duckdb_destroy_prepare(&stmt);
        return false;
    }

    duckdb_destroy_prepare(&stmt);
    return true;
}

// ──────────────────────────────────────────────────────────────────────
// 初始化
// ──────────────────────────────────────────────────────────────────────

bool DuckDBLogger::init(const String& db_path) {
    if (initialized_) {
        return true;
    }

    // 确保父目录存在
    std::filesystem::path p(db_path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    // 直接打开数据库（不设置配置选项，使用默认值）
    char* open_error = nullptr;
    duckdb_state state = duckdb_open_ext(db_path.c_str(), &db_, nullptr, &open_error);

    if (state != DuckDBSuccess) {
        SPDLOG_ERROR("[DuckDBLogger] Failed to open database: {}", open_error ? open_error : "unknown");
        if (open_error) duckdb_free(open_error);
        return false;
    }

    state = duckdb_connect(db_, &conn_);
    if (state != DuckDBSuccess) {
        SPDLOG_ERROR("[DuckDBLogger] Failed to create connection");
        duckdb_close(&db_);
        db_ = nullptr;
        return false;
    }

    // PRAGMA
    exec("PRAGMA threads=1");
    exec("PRAGMA auto_checkpoint=128");

    // 初始化表结构
    init_tables();

    // 初始化 node_io_logs 表
    init_node_io_table();

    // 初始化 tick_data 表
    init_tick_table();

    // 恢复 per-table ID 计数器
    auto recover_id = [this](const char* table, std::atomic<uint64_t>& counter) {
        duckdb_result res;
        std::string sql = std::string("SELECT COALESCE(MAX(id), 0) FROM ") + table;
        if (duckdb_query(conn_, sql.c_str(), &res) == DuckDBSuccess) {
            if (duckdb_row_count(&res) > 0) {
                auto* data = (int64_t*)duckdb_column_data(&res, 0);
                auto* nulls = duckdb_nullmask_data(&res, 0);
                if (data && !nulls[0] && data[0] >= 0) {
                    counter.store(static_cast<uint64_t>(data[0]) + 1);
                }
            }
        }
        duckdb_destroy_result(&res);
    };
    recover_id("strategy_logs", next_strategy_log_id_);
    recover_id("node_io_logs", next_node_io_id_);
    recover_id("tick_data", next_tick_id_);

    // 启动后台写入线程
    running_ = true;
    worker_thread_ = std::thread(&DuckDBLogger::worker_loop, this);

    initialized_ = true;
    SPDLOG_INFO("[DuckDBLogger] Initialized at {}", db_path);
    return true;
}

void DuckDBLogger::init_tables() {
    if (!table_exists("strategy_logs")) {
        exec(R"(
            CREATE TABLE strategy_logs (
                id BIGINT PRIMARY KEY,
                timestamp TIMESTAMP NOT NULL,
                strategy_name VARCHAR NOT NULL,
                level VARCHAR NOT NULL,
                message TEXT NOT NULL,
                context JSON
            )
        )");
    }

    exec("CREATE INDEX IF NOT EXISTS idx_strategy_time ON strategy_logs(strategy_name, timestamp)");
    exec("CREATE INDEX IF NOT EXISTS idx_level_time ON strategy_logs(level, timestamp)");
    exec("CREATE INDEX IF NOT EXISTS idx_time_desc ON strategy_logs(timestamp DESC)");
}

void DuckDBLogger::init_node_io_table() {
    if (!table_exists("node_io_logs")) {
        exec(R"(
            CREATE TABLE node_io_logs (
                id BIGINT PRIMARY KEY,
                timestamp TIMESTAMP NOT NULL,
                strategy_name VARCHAR NOT NULL,
                epoch BIGINT NOT NULL,
                node_type VARCHAR NOT NULL,
                node_id VARCHAR,
                input JSON,
                output JSON,
                metadata JSON
            )
        )");
    }

    exec("CREATE INDEX IF NOT EXISTS idx_nodeio_strategy_time ON node_io_logs(strategy_name, timestamp DESC)");
    exec("CREATE INDEX IF NOT EXISTS idx_nodeio_epoch ON node_io_logs(strategy_name, epoch)");
    exec("CREATE INDEX IF NOT EXISTS idx_nodeio_node_type ON node_io_logs(node_type)");
    exec("CREATE INDEX IF NOT EXISTS idx_nodeio_time ON node_io_logs(timestamp DESC)");
}

void DuckDBLogger::init_tick_table() {
    if (!table_exists("tick_data")) {
        exec(R"(
            CREATE TABLE tick_data (
                id              BIGINT PRIMARY KEY,
                timestamp       TIMESTAMP NOT NULL,
                symbol          TEXT NOT NULL,
                open            DOUBLE,
                close           DOUBLE,
                high            DOUBLE,
                low             DOUBLE,
                volume          BIGINT,
                turnover        BIGINT,
                value           DOUBLE,
                upper           DOUBLE,
                lower           DOUBLE,
                source          TEXT,
                confidence      INTEGER,
                bid_prices      DOUBLE[],
                bid_volumes     BIGINT[],
                ask_prices      DOUBLE[],
                ask_volumes     BIGINT[]
            )
        )");
    }

    exec("CREATE INDEX IF NOT EXISTS idx_tick_sym_time ON tick_data(symbol, timestamp)");
    exec("CREATE INDEX IF NOT EXISTS idx_tick_time ON tick_data(timestamp DESC)");
}

// ──────────────────────────────────────────────────────────────────────
// 异步日志入口
// ──────────────────────────────────────────────────────────────────────

void DuckDBLogger::log_strategy(
    const std::string& strategy_name,
    const std::string& level,
    const std::string& message,
    const std::string& context_json)
{
    if (!initialized_ || !running_) {
        return;
    }

    StrategyLogEntry entry;
    entry.timestamp = ToString(Now());
    entry.strategy_name = strategy_name;
    entry.level = level;
    entry.message = message;
    entry.context_json = context_json;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push(std::move(entry));
    }
    queue_cv_.notify_one();
}

void DuckDBLogger::log_node_io(
    const std::string& strategy_name,
    int64_t epoch,
    const std::string& node_type,
    const std::string& node_id,
    const std::string& input_json,
    const std::string& output_json,
    const std::string& metadata_json)
{
    if (!initialized_ || !running_) {
        return;
    }

    NodeIOEntry entry;
    entry.timestamp = ToString(Now());
    entry.strategy_name = strategy_name;
    entry.epoch = epoch;
    entry.node_type = node_type;
    entry.node_id = node_id;
    entry.input_json = input_json;
    entry.output_json = output_json;
    entry.metadata_json = metadata_json;

    {
        std::lock_guard<std::mutex> lock(node_io_queue_mutex_);
        node_io_queue_.push(std::move(entry));
    }
    node_io_queue_cv_.notify_one();
}

void DuckDBLogger::log_ticks(const std::vector<TickDataEntry>& ticks) {
    if (!initialized_ || !running_ || ticks.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(tick_queue_mutex_);
        for (auto& tick : ticks) {
            tick_queue_.push(tick);
        }
    }
    tick_queue_cv_.notify_one();
}

// ──────────────────────────────────────────────────────────────────────
// 后台写入线程
// ──────────────────────────────────────────────────────────────────────

void DuckDBLogger::worker_loop() {
    std::vector<StrategyLogEntry> batch;
    batch.reserve(BATCH_SIZE);
    std::vector<NodeIOEntry> node_io_batch;
    node_io_batch.reserve(BATCH_SIZE);
    std::vector<TickDataEntry> tick_batch;
    tick_batch.reserve(BATCH_SIZE);

    auto last_flush = std::chrono::steady_clock::now();

    while (running_ || !queue_.empty() || !node_io_queue_.empty() || !tick_queue_.empty()) {
        // 处理策略日志队列
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait_for(
                lock,
                std::chrono::milliseconds(FLUSH_INTERVAL_MS),
                [this] { return !queue_.empty() || !running_; }
            );
            while (!queue_.empty() && batch.size() < BATCH_SIZE) {
                batch.push_back(std::move(queue_.front()));
                queue_.pop();
            }
        }

        // 处理节点 IO 日志队列
        {
            std::unique_lock<std::mutex> lock(node_io_queue_mutex_);
            node_io_queue_cv_.wait_for(
                lock,
                std::chrono::milliseconds(FLUSH_INTERVAL_MS),
                [this] { return !node_io_queue_.empty() || !running_; }
            );
            while (!node_io_queue_.empty() && node_io_batch.size() < BATCH_SIZE) {
                node_io_batch.push_back(std::move(node_io_queue_.front()));
                node_io_queue_.pop();
            }
        }

        // 处理 Tick 数据队列
        {
            std::unique_lock<std::mutex> lock(tick_queue_mutex_);
            tick_queue_cv_.wait_for(
                lock,
                std::chrono::milliseconds(FLUSH_INTERVAL_MS),
                [this] { return !tick_queue_.empty() || !running_; }
            );
            while (!tick_queue_.empty() && tick_batch.size() < BATCH_SIZE) {
                tick_batch.push_back(std::move(tick_queue_.front()));
                tick_queue_.pop();
            }
        }

        if (!batch.empty()) {
            batch_insert(batch);
            batch.clear();
        }

        if (!node_io_batch.empty()) {
            batch_insert_node_io(node_io_batch);
            node_io_batch.clear();
        }

        if (!tick_batch.empty()) {
            batch_insert_ticks(tick_batch);
            tick_batch.clear();
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_flush > std::chrono::seconds(3)) {
            last_flush = now;
            exec("CHECKPOINT");
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// 批量插入
// ──────────────────────────────────────────────────────────────────────

void DuckDBLogger::batch_insert(const std::vector<StrategyLogEntry>& entries) {
    if (!exec("BEGIN TRANSACTION")) {
        return;
    }

    duckdb_prepared_statement stmt = nullptr;
    const char* insert_sql = R"(
        INSERT INTO strategy_logs
        (id, timestamp, strategy_name, level, message, context)
        VALUES (?, ?, ?, ?, ?, ?)
    )";

    if (duckdb_prepare(conn_, insert_sql, &stmt) != DuckDBSuccess) {
        exec("ROLLBACK");
        return;
    }

    bool failed = false;
    for (const auto& entry : entries) {
        duckdb_value v_id   = make_int64(static_cast<int64_t>(next_strategy_log_id_++));
        duckdb_value v_ts   = make_varchar(entry.timestamp);
        duckdb_value v_name = make_varchar(entry.strategy_name);
        duckdb_value v_lvl  = make_varchar(entry.level);
        duckdb_value v_msg  = make_varchar(entry.message);
        duckdb_value v_ctx  = entry.context_json.empty() ? make_null()
                                                         : make_varchar(entry.context_json);

        duckdb_value vals[6] = { v_id, v_ts, v_name, v_lvl, v_msg, v_ctx };

        for (int i = 0; i < 6; ++i) {
            if (!bind_value_at(stmt, (idx_t)(i + 1), vals[i])) {
                failed = true;
            }
        }
        // 创建后即可销毁（bind 会内部引用/复制）
        for (auto& v : vals) duckdb_destroy_value(&v);

        if (failed) break;

        duckdb_result result;
        if (duckdb_execute_prepared(stmt, &result) != DuckDBSuccess) {
            failed = true;
            duckdb_destroy_result(&result);
            break;
        }
        duckdb_destroy_result(&result);
    }

    duckdb_destroy_prepare(&stmt);

    if (failed) {
        exec("ROLLBACK");
        SPDLOG_ERROR("[DuckDBLogger] Batch insert failed, rolled back");
    } else {
        exec("COMMIT");
    }
}

void DuckDBLogger::batch_insert_node_io(const std::vector<NodeIOEntry>& entries) {
    if (!exec("BEGIN TRANSACTION")) {
        return;
    }

    duckdb_prepared_statement stmt = nullptr;
    const char* insert_sql = R"(
        INSERT INTO node_io_logs
        (id, timestamp, strategy_name, epoch, node_type, node_id, input, output, metadata)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    if (duckdb_prepare(conn_, insert_sql, &stmt) != DuckDBSuccess) {
        exec("ROLLBACK");
        return;
    }

    bool failed = false;
    for (const auto& entry : entries) {
        duckdb_value v_id   = make_int64(static_cast<int64_t>(next_node_io_id_++));
        duckdb_value v_ts   = make_varchar(entry.timestamp);
        duckdb_value v_name = make_varchar(entry.strategy_name);
        duckdb_value v_epoch = make_int64(entry.epoch);
        duckdb_value v_type = make_varchar(entry.node_type);
        duckdb_value v_nid  = make_varchar(entry.node_id);
        duckdb_value v_in   = entry.input_json.empty() ? make_null() : make_varchar(entry.input_json);
        duckdb_value v_out  = entry.output_json.empty() ? make_null() : make_varchar(entry.output_json);
        duckdb_value v_meta = entry.metadata_json.empty() ? make_null() : make_varchar(entry.metadata_json);

        duckdb_value vals[9] = { v_id, v_ts, v_name, v_epoch, v_type, v_nid, v_in, v_out, v_meta };

        for (int i = 0; i < 9; ++i) {
            if (!bind_value_at(stmt, (idx_t)(i + 1), vals[i])) {
                failed = true;
            }
        }
        for (auto& v : vals) duckdb_destroy_value(&v);

        if (failed) break;

        duckdb_result result;
        if (duckdb_execute_prepared(stmt, &result) != DuckDBSuccess) {
            failed = true;
            duckdb_destroy_result(&result);
            break;
        }
        duckdb_destroy_result(&result);
    }

    duckdb_destroy_prepare(&stmt);

    if (failed) {
        exec("ROLLBACK");
        SPDLOG_ERROR("[DuckDBLogger] Node IO batch insert failed, rolled back");
    } else {
        exec("COMMIT");
    }
}

// ──────────────────────────────────────────────────────────────────────
// 辅助：创建 DuckDB LIST 值（数组）
// ──────────────────────────────────────────────────────────────────────

namespace {
duckdb_value make_double_list(const std::vector<double>& values) {
    if (values.empty()) return duckdb_create_null_value();
    std::vector<duckdb_value> elems;
    elems.reserve(values.size());
    for (auto v : values) elems.push_back(duckdb_create_double(v));
    auto list_type = duckdb_create_list_type(duckdb_create_logical_type(DUCKDB_TYPE_DOUBLE));
    duckdb_value result = duckdb_create_list_value(list_type, elems.data(), elems.size());
    duckdb_destroy_logical_type(&list_type);
    for (auto& e : elems) duckdb_destroy_value(&e);
    return result;
}

duckdb_value make_int64_list(const std::vector<int64_t>& values) {
    if (values.empty()) return duckdb_create_null_value();
    std::vector<duckdb_value> elems;
    elems.reserve(values.size());
    for (auto v : values) elems.push_back(duckdb_create_int64(v));
    auto list_type = duckdb_create_list_type(duckdb_create_logical_type(DUCKDB_TYPE_BIGINT));
    duckdb_value result = duckdb_create_list_value(list_type, elems.data(), elems.size());
    duckdb_destroy_logical_type(&list_type);
    for (auto& e : elems) duckdb_destroy_value(&e);
    return result;
}
}

// ──────────────────────────────────────────────────────────────────────
// 批量插入 Tick 数据
// ──────────────────────────────────────────────────────────────────────

void DuckDBLogger::batch_insert_ticks(const std::vector<TickDataEntry>& entries) {
    if (!exec("BEGIN TRANSACTION")) {
        return;
    }

    duckdb_prepared_statement stmt = nullptr;
    const char* insert_sql = R"(
        INSERT INTO tick_data
        (id, timestamp, symbol, open, close, high, low, volume, turnover, value,
         upper, lower, source, confidence, bid_prices, bid_volumes, ask_prices, ask_volumes)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    if (duckdb_prepare(conn_, insert_sql, &stmt) != DuckDBSuccess) {
        const char* err = duckdb_prepare_error(stmt);
        SPDLOG_ERROR("[DuckDBLogger] Tick batch prepare failed: {}", err ? err : "unknown");
        duckdb_destroy_prepare(&stmt);
        exec("ROLLBACK");
        return;
    }

    bool failed = false;
    String last_error;
    size_t failed_idx = 0;
    for (size_t idx = 0; idx < entries.size(); ++idx) {
        const auto& entry = entries[idx];
        // timestamp: epoch seconds → DuckDB TIMESTAMP via datetime string
        std::string ts_str = ToString(static_cast<time_t>(entry.timestamp_epoch));

        duckdb_value v_id   = make_int64(static_cast<int64_t>(next_tick_id_++));
        duckdb_value v_ts   = make_varchar(ts_str);
        duckdb_value v_sym  = make_varchar(entry.symbol);
        duckdb_value v_open = duckdb_create_double(entry.open);
        duckdb_value v_close = duckdb_create_double(entry.close);
        duckdb_value v_high = duckdb_create_double(entry.high);
        duckdb_value v_low  = duckdb_create_double(entry.low);
        duckdb_value v_vol  = make_int64(entry.volume);
        duckdb_value v_turn = make_int64(entry.turnover);
        duckdb_value v_val  = duckdb_create_double(entry.value);
        duckdb_value v_upper = duckdb_create_double(entry.upper);
        duckdb_value v_lower = duckdb_create_double(entry.lower);
        duckdb_value v_src  = entry.source.empty() ? make_null() : make_varchar(entry.source);
        duckdb_value v_conf = make_int32(entry.confidence);

        // 盘口：有数据时才写入数组，否则 NULL
        bool has_book = (!entry.bid_prices.empty() && entry.bid_prices[0] > 0);
        duckdb_value v_bid_p = has_book ? make_double_list(entry.bid_prices) : make_null();
        duckdb_value v_bid_v = has_book ? make_int64_list(entry.bid_volumes) : make_null();
        duckdb_value v_ask_p = has_book ? make_double_list(entry.ask_prices) : make_null();
        duckdb_value v_ask_v = has_book ? make_int64_list(entry.ask_volumes) : make_null();

        duckdb_value vals[18] = {
            v_id, v_ts, v_sym, v_open, v_close, v_high, v_low, v_vol, v_turn,
            v_val, v_upper, v_lower, v_src, v_conf,
            v_bid_p, v_bid_v, v_ask_p, v_ask_v
        };

        for (int i = 0; i < 18; ++i) {
            if (!bind_value_at(stmt, (idx_t)(i + 1), vals[i])) {
                failed = true;
                failed_idx = idx;
                last_error = "bind_value_at failed at column " + std::to_string(i + 1);
            }
        }
        for (auto& v : vals) duckdb_destroy_value(&v);

        if (failed) break;

        duckdb_result result;
        if (duckdb_execute_prepared(stmt, &result) != DuckDBSuccess) {
            failed = true;
            failed_idx = idx;
            const char* err = duckdb_result_error(&result);
            last_error = err ? err : "duckdb_execute_prepared failed";
            duckdb_destroy_result(&result);
            break;
        }
        duckdb_destroy_result(&result);
    }

    duckdb_destroy_prepare(&stmt);

    if (failed) {
        exec("ROLLBACK");
        const auto& e = entries[failed_idx];
        SPDLOG_ERROR("[DuckDBLogger] Tick batch insert failed at entry [{}/{}]: symbol={}, error={}",
                     failed_idx, entries.size(), e.symbol, last_error);
    } else {
        exec("COMMIT");
    }
}

// ──────────────────────────────────────────────────────────────────────
// 查询策略日志
// ──────────────────────────────────────────────────────────────────────

std::vector<StrategyLogEntry> DuckDBLogger::query_strategy_logs(
    const std::string& strategy_name,
    const std::string& keyword,
    const std::string& level_filter,
    const std::string& start_time,
    const std::string& end_time,
    int limit,
    int offset)
{
    if (!initialized_) {
        return {};
    }

    std::vector<StrategyLogEntry> results;

    std::string sql = "SELECT id, timestamp, strategy_name, level, message, context FROM strategy_logs WHERE 1=1";
    std::vector<duckdb_value> params;

    if (!strategy_name.empty()) {
        sql += " AND strategy_name = ?";
        params.push_back(make_varchar(strategy_name));
    }
    if (!keyword.empty()) {
        sql += " AND message LIKE ?";
        params.push_back(make_varchar("%" + keyword + "%"));
    }
    if (!level_filter.empty()) {
        sql += " AND level = ?";
        params.push_back(make_varchar(level_filter));
    }
    if (!start_time.empty()) {
        sql += " AND timestamp >= ?";
        params.push_back(make_varchar(start_time));
    }
    if (!end_time.empty()) {
        sql += " AND timestamp <= ?";
        params.push_back(make_varchar(end_time));
    }

    sql += " ORDER BY timestamp DESC LIMIT ? OFFSET ?";
    params.push_back(make_int32(limit));
    params.push_back(make_int32(offset));

    duckdb_result result;
    if (!query_params(sql, params, result)) {
        // 清理 params
        for (auto& v : params) duckdb_destroy_value(&v);
        return {};
    }

    // 清理 params（query_params 完成后即可销毁）
    for (auto& v : params) duckdb_destroy_value(&v);

    idx_t row_count = duckdb_row_count(&result);

    for (idx_t i = 0; i < row_count; i++) {
        StrategyLogEntry entry;

        // 使用 duckdb_value_string 读取（带长度，避免 null 截断）
        auto read_str = [&](idx_t col) -> std::string {
            duckdb_string val = duckdb_value_string(&result, col, i);
            if (!val.data || val.size == 0) return "";
            std::string s(val.data, val.size);
            duckdb_free(val.data);
            return s;
        };

        // id
        entry.id = duckdb_value_int64(&result, 0, i);

        entry.timestamp     = read_str(1);
        entry.strategy_name = read_str(2);
        entry.level         = read_str(3);
        entry.message       = read_str(4);
        entry.context_json  = read_str(5);

        results.push_back(std::move(entry));
    }

    duckdb_destroy_result(&result);
    return results;
}

// ──────────────────────────────────────────────────────────────────────
// 查询符合条件的日志总数
// ──────────────────────────────────────────────────────────────────────

int DuckDBLogger::count_strategy_logs(
    const std::string& strategy_name,
    const std::string& keyword,
    const std::string& level_filter,
    const std::string& start_time,
    const std::string& end_time)
{
    if (!initialized_) {
        return 0;
    }

    std::string sql = "SELECT COUNT(*) FROM strategy_logs WHERE 1=1";
    std::vector<duckdb_value> params;

    if (!strategy_name.empty()) {
        sql += " AND strategy_name = ?";
        params.push_back(make_varchar(strategy_name));
    }
    if (!keyword.empty()) {
        sql += " AND message LIKE ?";
        params.push_back(make_varchar("%" + keyword + "%"));
    }
    if (!level_filter.empty()) {
        sql += " AND level = ?";
        params.push_back(make_varchar(level_filter));
    }
    if (!start_time.empty()) {
        sql += " AND timestamp >= ?";
        params.push_back(make_varchar(start_time));
    }
    if (!end_time.empty()) {
        sql += " AND timestamp <= ?";
        params.push_back(make_varchar(end_time));
    }

    duckdb_result result;
    if (!query_params(sql, params, result)) {
        for (auto& v : params) duckdb_destroy_value(&v);
        return 0;
    }

    for (auto& v : params) duckdb_destroy_value(&v);

    int count = 0;
    idx_t row_count = duckdb_row_count(&result);
    if (row_count > 0) {
        auto* count_data = (int32_t*)duckdb_column_data(&result, 0);
        if (count_data) {
            count = *count_data;
        }
    }

    duckdb_destroy_result(&result);
    return count;
}

// ──────────────────────────────────────────────────────────────────────
// 策略统计
// ──────────────────────────────────────────────────────────────────────

DuckDBLogger::StrategyStats DuckDBLogger::get_strategy_stats(
    const std::string& start_time,
    const std::string& end_time)
{
    StrategyStats stats;

    if (!initialized_) {
        return stats;
    }

    std::string where_clause = "WHERE 1=1";
    std::vector<duckdb_value> params;

    if (!start_time.empty()) {
        where_clause += " AND timestamp >= ?";
        params.push_back(make_varchar(start_time));
    }
    if (!end_time.empty()) {
        where_clause += " AND timestamp <= ?";
        params.push_back(make_varchar(end_time));
    }

    auto query_count = [&](const std::string& extra_cond) -> int64_t {
        std::string sql = "SELECT COUNT(*) FROM strategy_logs " + where_clause + extra_cond;
        duckdb_result result;
        if (query_params(sql, params, result)) {
            if (duckdb_row_count(&result) > 0) {
                auto* data = (int64_t*)duckdb_column_data(&result, 0);
                auto* null_mask = duckdb_nullmask_data(&result, 0);
                int64_t cnt = (data && !null_mask[0]) ? data[0] : 0;
                duckdb_destroy_result(&result);
                return cnt;
            }
            duckdb_destroy_result(&result);
        }
        return 0;
    };

    stats.total_logs = (int)query_count("");
    stats.error_count = (int)query_count(" AND level = 'ERROR'");
    stats.warn_count = (int)query_count(" AND level = 'WARN'");

    // 按策略分组
    {
        std::string sql = "SELECT strategy_name, COUNT(*) as cnt FROM strategy_logs " +
                          where_clause + " GROUP BY strategy_name ORDER BY cnt DESC";
        duckdb_result result;
        if (query_params(sql, params, result)) {
            idx_t row_count = duckdb_row_count(&result);

            for (idx_t i = 0; i < row_count; i++) {
                duckdb_string name_val = duckdb_value_string(&result, 0, i);
                if (name_val.data && name_val.size > 0) {
                    std::string name(name_val.data, name_val.size);
                    duckdb_free(name_val.data);
                    auto* cnt_data = (int64_t*)duckdb_column_data(&result, 1);
                    auto* cnt_null = duckdb_nullmask_data(&result, 1);
                    if (cnt_data && !cnt_null[i]) {
                        stats.strategy_counts[name] = (int)cnt_data[i];
                    }
                }
            }
            duckdb_destroy_result(&result);
        }
    }

    // 有错误的策略
    {
        std::string sql = "SELECT strategy_name, COUNT(*) as cnt FROM strategy_logs " +
                          where_clause + " AND level = 'ERROR' GROUP BY strategy_name ORDER BY cnt DESC";
        duckdb_result result;
        if (query_params(sql, params, result)) {
            idx_t row_count = duckdb_row_count(&result);

            for (idx_t i = 0; i < row_count; i++) {
                duckdb_string name_val = duckdb_value_string(&result, 0, i);
                if (name_val.data && name_val.size > 0) {
                    std::string name(name_val.data, name_val.size);
                    duckdb_free(name_val.data);
                    auto* cnt_data = (int64_t*)duckdb_column_data(&result, 1);
                    auto* cnt_null = duckdb_nullmask_data(&result, 1);
                    if (cnt_data && !cnt_null[i]) {
                        stats.error_strategies[name] = (int)cnt_data[i];
                    }
                }
            }
            duckdb_destroy_result(&result);
        }
    }

    // 清理 params
    for (auto& v : params) duckdb_destroy_value(&v);

    return stats;
}

// ──────────────────────────────────────────────────────────────────────
// 清理旧日志
// ──────────────────────────────────────────────────────────────────────

void DuckDBLogger::cleanup_old_logs(int retention_days) {
    if (!initialized_) {
        return;
    }

    auto cutoff_date = ToString(Now() - retention_days * 86400);
    duckdb_value v = make_varchar(cutoff_date);
    exec_params("DELETE FROM strategy_logs WHERE timestamp < ?", {v});
    duckdb_destroy_value(&v);

    exec("VACUUM");

    SPDLOG_INFO("[DuckDBLogger] Cleaned logs older than {} days", retention_days);
}

DuckDBLogger::DeleteResult DuckDBLogger::delete_strategy_logs(
    const std::string& strategy_name,
    const std::string& level,
    const std::string& start_time,
    const std::string& end_time)
{
    DeleteResult result = {0, ""};

    if (!initialized_) {
        result.error = "DuckDB logger not initialized";
        result.deleted_count = -1;
        return result;
    }

    // 至少需要一个过滤条件
    if (strategy_name.empty() && level.empty() && start_time.empty() && end_time.empty()) {
        result.error = "At least one filter condition is required";
        result.deleted_count = -1;
        return result;
    }

    // 先查询将要删除的数量
    int count = count_strategy_logs(strategy_name, "", level, start_time, end_time);

    // 构建 DELETE SQL
    std::string sql = "DELETE FROM strategy_logs WHERE 1=1";
    std::vector<duckdb_value> params;

    if (!strategy_name.empty()) {
        sql += " AND strategy_name = ?";
        params.push_back(make_varchar(strategy_name));
    }
    if (!level.empty()) {
        sql += " AND level = ?";
        params.push_back(make_varchar(level));
    }
    if (!start_time.empty()) {
        sql += " AND timestamp >= ?";
        params.push_back(make_varchar(start_time));
    }
    if (!end_time.empty()) {
        sql += " AND timestamp <= ?";
        params.push_back(make_varchar(end_time));
    }

    bool success = exec_params(sql, params);
    for (auto& v : params) duckdb_destroy_value(&v);

    if (!success) {
        result.error = "DELETE execution failed";
        result.deleted_count = -1;
        return result;
    }

    // VACUUM 回收空间
    exec("VACUUM");

    result.deleted_count = count;
    SPDLOG_INFO("[DuckDBLogger] Deleted {} strategy logs", count);
    return result;
}

// ──────────────────────────────────────────────────────────────────────
// 节点 IO 日志查询
// ──────────────────────────────────────────────────────────────────────

std::vector<NodeIOEntry> DuckDBLogger::query_node_io_logs(
    const std::string& strategy_name,
    const std::string& node_type,
    int64_t epoch_from,
    int64_t epoch_to,
    const std::string& start_time,
    const std::string& end_time,
    int limit,
    int offset)
{
    if (!initialized_) {
        return {};
    }

    std::vector<NodeIOEntry> results;

    std::string sql = "SELECT id, timestamp, strategy_name, epoch, node_type, node_id, input, output, metadata FROM node_io_logs WHERE 1=1";
    std::vector<duckdb_value> params;

    if (!strategy_name.empty()) {
        sql += " AND strategy_name = ?";
        params.push_back(make_varchar(strategy_name));
    }
    if (!node_type.empty()) {
        sql += " AND node_type = ?";
        params.push_back(make_varchar(node_type));
    }
    if (epoch_from > 0) {
        sql += " AND epoch >= ?";
        params.push_back(make_int64(epoch_from));
    }
    if (epoch_to > 0) {
        sql += " AND epoch <= ?";
        params.push_back(make_int64(epoch_to));
    }
    if (!start_time.empty()) {
        sql += " AND timestamp >= ?";
        params.push_back(make_varchar(start_time));
    }
    if (!end_time.empty()) {
        sql += " AND timestamp <= ?";
        params.push_back(make_varchar(end_time));
    }

    sql += " ORDER BY timestamp DESC LIMIT ? OFFSET ?";
    params.push_back(make_int32(limit));
    params.push_back(make_int32(offset));

    duckdb_result result;
    if (!query_params(sql, params, result)) {
        for (auto& v : params) duckdb_destroy_value(&v);
        return {};
    }

    for (auto& v : params) duckdb_destroy_value(&v);

    idx_t row_count = duckdb_row_count(&result);

    for (idx_t i = 0; i < row_count; i++) {
        NodeIOEntry entry;
        entry.id = duckdb_value_int64(&result, 0, i);

        // 使用 duckdb_value_string 读取（带长度，避免 null 截断）
        auto read_str = [&](idx_t col) -> std::string {
            duckdb_string val = duckdb_value_string(&result, col, i);
            if (!val.data || val.size == 0) return "";
            std::string s(val.data, val.size);
            duckdb_free(val.data);
            return s;
        };

        entry.timestamp     = read_str(1);
        entry.strategy_name = read_str(2);
        entry.epoch         = duckdb_value_int64(&result, 3, i);
        entry.node_type     = read_str(4);
        entry.node_id       = read_str(5);
        entry.input_json    = read_str(6);
        entry.output_json   = read_str(7);
        entry.metadata_json = read_str(8);

        results.push_back(std::move(entry));
    }

    duckdb_destroy_result(&result);
    return results;
}

int DuckDBLogger::count_node_io_logs(
    const std::string& strategy_name,
    const std::string& node_type,
    int64_t epoch_from,
    int64_t epoch_to,
    const std::string& start_time,
    const std::string& end_time)
{
    if (!initialized_) {
        return 0;
    }

    std::string sql = "SELECT COUNT(*) FROM node_io_logs WHERE 1=1";
    std::vector<duckdb_value> params;

    if (!strategy_name.empty()) {
        sql += " AND strategy_name = ?";
        params.push_back(make_varchar(strategy_name));
    }
    if (!node_type.empty()) {
        sql += " AND node_type = ?";
        params.push_back(make_varchar(node_type));
    }
    if (epoch_from > 0) {
        sql += " AND epoch >= ?";
        params.push_back(make_int64(epoch_from));
    }
    if (epoch_to > 0) {
        sql += " AND epoch <= ?";
        params.push_back(make_int64(epoch_to));
    }
    if (!start_time.empty()) {
        sql += " AND timestamp >= ?";
        params.push_back(make_varchar(start_time));
    }
    if (!end_time.empty()) {
        sql += " AND timestamp <= ?";
        params.push_back(make_varchar(end_time));
    }

    duckdb_result result;
    if (!query_params(sql, params, result)) {
        for (auto& v : params) duckdb_destroy_value(&v);
        return 0;
    }

    for (auto& v : params) duckdb_destroy_value(&v);

    int count = 0;
    idx_t row_count = duckdb_row_count(&result);
    if (row_count > 0) {
        auto* count_data = (int32_t*)duckdb_column_data(&result, 0);
        if (count_data) count = *count_data;
    }

    duckdb_destroy_result(&result);
    return count;
}

int64_t DuckDBLogger::delete_node_io_logs_before(const std::string& timestamp) {
    if (!initialized_) {
        return 0;
    }

    // 先查询删除前的数量
    int before_count = count_node_io_logs("", "", 0, 0, "", timestamp);

    duckdb_value v = make_varchar(timestamp);
    exec_params("DELETE FROM node_io_logs WHERE timestamp < ?", {v});
    duckdb_destroy_value(&v);

    exec("VACUUM");

    SPDLOG_INFO("[DuckDBLogger] Deleted node IO logs before {}, approximately {} rows", timestamp, before_count);
    return before_count;
}

// ──────────────────────────────────────────────────────────────────────
// 关闭
// ──────────────────────────────────────────────────────────────────────

void DuckDBLogger::shutdown() {
    if (!running_ && !initialized_) {
        return;
    }

    running_ = false;
    queue_cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    if (conn_) {
        exec("CHECKPOINT");
        duckdb_disconnect(&conn_);
        conn_ = nullptr;
    }
    if (db_) {
        duckdb_close(&db_);
        db_ = nullptr;
    }

    initialized_ = false;
    SPDLOG_INFO("[DuckDBLogger] Shutdown complete");
}

// ──────────────────────────────────────────────────────────────────────
// Tick 数据查询
// ──────────────────────────────────────────────────────────────────────

std::vector<TickDataEntry> DuckDBLogger::query_ticks(
    const std::string& symbol,
    int64_t start_ts,
    int64_t end_ts,
    int limit)
{
    if (!initialized_) {
        return {};
    }

    std::vector<TickDataEntry> results;

    std::string sql = "SELECT id, timestamp, symbol, open, close, high, low, volume, turnover, "
                      "value, upper, lower, source, confidence, bid_prices, bid_volumes, "
                      "ask_prices, ask_volumes FROM tick_data WHERE 1=1";
    std::vector<duckdb_value> params;

    if (!symbol.empty()) {
        sql += " AND symbol = ?";
        params.push_back(make_varchar(symbol));
    }
    if (start_ts > 0) {
        sql += " AND timestamp >= ?";
        params.push_back(make_varchar(ToString(static_cast<time_t>(start_ts))));
    }
    if (end_ts < INT64_MAX) {
        sql += " AND timestamp <= ?";
        params.push_back(make_varchar(ToString(static_cast<time_t>(end_ts))));
    }

    sql += " ORDER BY timestamp ASC LIMIT ?";
    params.push_back(make_int32(limit));

    duckdb_result result;
    if (!query_params(sql, params, result)) {
        for (auto& v : params) duckdb_destroy_value(&v);
        return {};
    }

    for (auto& v : params) duckdb_destroy_value(&v);

    idx_t row_count = duckdb_row_count(&result);

    for (idx_t i = 0; i < row_count; i++) {
        TickDataEntry entry;

        auto read_str = [&](idx_t col) -> std::string {
            duckdb_string val = duckdb_value_string(&result, col, i);
            if (!val.data || val.size == 0) return "";
            std::string s(val.data, val.size);
            duckdb_free(val.data);
            return s;
        };

        entry.id = duckdb_value_int64(&result, 0, i);

        // timestamp: 解析 datetime 字符串 ("2026-07-03 13:35:38") → epoch seconds
        std::string ts_str = read_str(1);
        entry.timestamp_epoch = ts_str.empty() ? 0 : static_cast<int64_t>(FromStr(ts_str, "%Y-%m-%d %H:%M:%S"));

        entry.symbol     = read_str(2);
        entry.open       = duckdb_value_double(&result, 3, i);
        entry.close      = duckdb_value_double(&result, 4, i);
        entry.high       = duckdb_value_double(&result, 5, i);
        entry.low        = duckdb_value_double(&result, 6, i);
        entry.volume     = duckdb_value_int64(&result, 7, i);
        entry.turnover   = duckdb_value_int64(&result, 8, i);
        entry.value      = duckdb_value_double(&result, 9, i);
        entry.upper      = duckdb_value_double(&result, 10, i);
        entry.lower      = duckdb_value_double(&result, 11, i);
        entry.source     = read_str(12);

        // confidence
        auto* conf_data = (int32_t*)duckdb_column_data(&result, 13);
        auto* conf_null = duckdb_nullmask_data(&result, 13);
        entry.confidence = (conf_data && !conf_null[i]) ? conf_data[i] : 0;

        // 盘口数组（LIST 类型）— 使用 duckdb_value_string 读取为字符串表示
        // 复盘查询中盘口数据通常不需要，这里简化处理：不解析 LIST，留给前端按需请求
        // 如需盘口，可在后续扩展

        results.push_back(std::move(entry));
    }

    duckdb_destroy_result(&result);
    return results;
}

int64_t DuckDBLogger::delete_tick_data_before(int64_t timestamp_epoch) {
    if (!initialized_) return -1;

    std::string sql = "DELETE FROM tick_data WHERE timestamp < ?";
    duckdb_value v = make_varchar(ToString(static_cast<time_t>(timestamp_epoch)));
    bool success = exec_params(sql, {v});
    duckdb_destroy_value(&v);

    if (!success) return -1;

    exec("VACUUM");
    SPDLOG_INFO("[DuckDBLogger] Deleted tick data before {}", timestamp_epoch);
    return 0;  // DuckDB C API 不返回删除行数，返回 0 表示成功
}
