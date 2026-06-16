#include "Util/DuckDBLogger.h"
#include "Util/datetime.h"
#include "Util/string_algorithm.h"
#include <chrono>
#include <algorithm>
#include <filesystem>

namespace {
    // 获取当前线程ID的字符串表示
    std::string get_thread_id() {
        std::hash<std::thread::id> hasher;
        return std::to_string(hasher(std::this_thread::get_id()));
    }
}

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

bool DuckDBLogger::exec(const std::string& sql) {
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

bool DuckDBLogger::exec_params(const std::string& sql, const std::vector<duckdb_value>& params) {
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

bool DuckDBLogger::query_params(const std::string& sql,
                                const std::vector<duckdb_value>& params,
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

bool DuckDBLogger::init(const std::string& db_path) {
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

    // 初始化表结构
    init_tables();

    // 初始化ID计数器
    duckdb_result result;
    if (query_params("SELECT COALESCE(MAX(id), 0) + 1 FROM strategy_logs", {}, result)) {
        if (duckdb_row_count(&result) > 0) {
            auto* data = (int64_t*)duckdb_column_data(&result, 0);
            auto* null_mask = duckdb_nullmask_data(&result, 0);
            if (data && !null_mask[0]) id_counter_ = data[0];
        }
        duckdb_destroy_result(&result);
    } else {
        id_counter_ = 1;
    }

    // 启动后台写入线程
    running_ = true;
    worker_thread_ = std::thread(&DuckDBLogger::worker_loop, this);

    initialized_ = true;
    SPDLOG_INFO("[DuckDBLogger] Initialized at {}", db_path);
    return true;
}

void DuckDBLogger::init_tables() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS strategy_logs (
            id BIGINT PRIMARY KEY,
            timestamp TIMESTAMP NOT NULL,
            strategy_name VARCHAR NOT NULL,
            level VARCHAR NOT NULL,
            message TEXT NOT NULL,
            context JSON
        )
    )");

    exec("CREATE INDEX IF NOT EXISTS idx_strategy_time ON strategy_logs(strategy_name, timestamp)");
    exec("CREATE INDEX IF NOT EXISTS idx_level_time ON strategy_logs(level, timestamp)");
    exec("CREATE INDEX IF NOT EXISTS idx_time_desc ON strategy_logs(timestamp DESC)");
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
    entry.id = id_counter_++;
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

// ──────────────────────────────────────────────────────────────────────
// 后台写入线程
// ──────────────────────────────────────────────────────────────────────

void DuckDBLogger::worker_loop() {
    std::vector<StrategyLogEntry> batch;
    batch.reserve(BATCH_SIZE);

    auto last_flush = std::chrono::steady_clock::now();

    while (running_ || !queue_.empty()) {
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

        lock.unlock();

        if (!batch.empty()) {
            batch_insert(batch);
            batch.clear();
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_flush > std::chrono::seconds(3)) {
            // 新版 DuckDB C API 不支持 wal_checkpoint PRAGMA，跳过
            // exec("PRAGMA wal_checkpoint(PASSIVE)");
            last_flush = now;
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
        duckdb_value v_id   = make_int64(entry.id);
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
        // 新版 DuckDB C API 不支持 wal_checkpoint PRAGMA，直接断开
        // exec("PRAGMA wal_checkpoint(FULL)");
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
