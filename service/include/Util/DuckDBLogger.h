#pragma once
#include "std_header.h"
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <vector>
#include <string>

// DuckDB C API
extern "C" {
#include "duckdb.h"
}

/**
 * 策略执行日志条目
 */
struct StrategyLogEntry {
    int64_t id;
    std::string timestamp;      // YYYY-MM-DD HH:MM:SS.mmm
    std::string strategy_name;  // 策略名称
    std::string level;          // INFO/WARN/ERROR
    std::string message;        // 日志内容
    std::string context_json;   // 结构化上下文（信号、订单等）
};

/**
 * DuckDB 策略日志管理器（单例）
 *
 * 职责：
 * - 异步批量写入策略执行日志到 DuckDB
 * - 提供SQL查询接口（供HTTP API使用）
 * - 启动时自动初始化表结构
 *
 * 使用方式：
 *   DuckDBLogger::instance().log_strategy("MA_Cross", "INFO", "买入信号触发");
 */
class DuckDBLogger {
public:
    static DuckDBLogger& instance();

    /**
     * 初始化日志数据库（服务启动时调用）
     * @param db_path 数据库文件路径（默认：logs/strategy_logs.db）
     * @return 是否成功
     */
    bool init(const std::string& db_path = "logs/strategy_logs.db");

    /**
     * 记录策略日志（异步，立即返回）
     * @param strategy_name 策略名称
     * @param level 日志级别（INFO/WARN/ERROR）
     * @param message 日志消息
     * @param context_json 可选的JSON上下文
     */
    void log_strategy(
        const std::string& strategy_name,
        const std::string& level,
        const std::string& message,
        const std::string& context_json = ""
    );

    /**
     * 查询策略日志（同步，供HTTP API使用）
     */
    std::vector<StrategyLogEntry> query_strategy_logs(
        const std::string& strategy_name = "",
        const std::string& keyword = "",
        const std::string& level_filter = "",
        const std::string& start_time = "",
        const std::string& end_time = "",
        int limit = 1000,
        int offset = 0
    );

    /**
     * 查询符合条件的日志总数（不受 limit/offset 限制）
     */
    int count_strategy_logs(
        const std::string& strategy_name = "",
        const std::string& keyword = "",
        const std::string& level_filter = "",
        const std::string& start_time = "",
        const std::string& end_time = ""
    );

    /**
     * 获取策略执行统计
     */
    struct StrategyStats {
        int total_logs;
        int error_count;
        int warn_count;
        std::map<std::string, int> strategy_counts;  // 策略名 -> 日志数
        std::map<std::string, int> error_strategies;  // 策略名 -> 错误数
    };
    StrategyStats get_strategy_stats(
        const std::string& start_time = "",
        const std::string& end_time = ""
    );

    /**
     * 清理旧日志（按保留策略）
     */
    void cleanup_old_logs(int retention_days = 90);

    /**
     * 关闭日志器（服务退出时调用）
     */
    void shutdown();

    /**
     * 检查是否已初始化
     */
    bool is_initialized() const { return initialized_; }

private:
    DuckDBLogger() = default;
    ~DuckDBLogger();

    // 禁止拷贝
    DuckDBLogger(const DuckDBLogger&) = delete;
    DuckDBLogger& operator=(const DuckDBLogger&) = delete;

    // 初始化表结构
    void init_tables();

    // 后台写入线程主循环
    void worker_loop();

    // 批量写入DuckDB
    void batch_insert(const std::vector<StrategyLogEntry>& entries);

    // 执行 SQL（无参数）
    bool exec(const std::string& sql);

    // 准备 + 执行 SQL（带参数，使用 duckdb_value 数组）
    bool exec_params(const std::string& sql, const std::vector<duckdb_value>& params);

    // 查询 + 回调（带参数，使用 duckdb_value 数组）
    bool query_params(const std::string& sql,
                      const std::vector<duckdb_value>& params,
                      duckdb_result& out_result);

    // 辅助：绑定 duckdb_value 到 prepared statement
    static bool bind_value_at(duckdb_prepared_statement stmt, idx_t index, duckdb_value val);

    // 辅助：创建 varchar 值
    static duckdb_value make_varchar(const std::string& str);

    // 辅助：创建 int32 值
    static duckdb_value make_int32(int32_t v);

    // 辅助：创建 int64 值
    static duckdb_value make_int64(int64_t v);

    // 辅助：创建 NULL 值
    static duckdb_value make_null();

    // C API 句柄
    duckdb_database db_ = nullptr;
    duckdb_connection conn_ = nullptr;

    // 异步队列
    std::queue<StrategyLogEntry> queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::thread worker_thread_;

    std::atomic<int64_t> id_counter_{0};

    // 批量写入配置
    static constexpr int BATCH_SIZE = 200;          // 批量阈值
    static constexpr int FLUSH_INTERVAL_MS = 500;   // 强制刷新间隔
};
