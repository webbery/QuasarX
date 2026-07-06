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
 * 节点输入输出日志条目
 */
struct NodeIOEntry {
    int64_t id;
    std::string timestamp;          // YYYY-MM-DD HH:MM:SS.mmm
    std::string strategy_name;      // 策略名称
    int64_t epoch;                  // 执行周期序号
    std::string node_type;          // input/signal/portfolio/execution
    std::string node_id;            // 节点 ID
    std::string input_json;         // 输入数据 JSON
    std::string output_json;        // 输出数据 JSON
    std::string metadata_json;      // 元数据 JSON
};

/**
 * Tick 数据条目
 *
 * 盘口使用 REAL[]/BIGINT[] 数组存储，DuckDB 对 NULL 元素几乎零开销。
 * 只有 _bidPrice[0] > 0 时才写入盘口数组，否则存 NULL。
 */
struct TickDataEntry {
    int64_t id;
    int64_t timestamp_epoch;    // Unix 时间戳
    std::string symbol;         // 标的代码
    double open;
    double close;
    double high;
    double low;
    int64_t volume;
    int64_t turnover;
    double value;
    double upper;
    double lower;
    std::string source;
    int confidence;
    // 盘口：仅当 _bidPrice[0] > 0 时填充，否则为空数组
    std::vector<double> bid_prices;
    std::vector<int64_t> bid_volumes;
    std::vector<double> ask_prices;
    std::vector<int64_t> ask_volumes;
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
     * 记录节点输入输出日志（异步，立即返回）
     * @param strategy_name 策略名称
     * @param epoch 执行周期序号
     * @param node_type 节点类型（input/signal/portfolio/execution）
     * @param node_id 节点 ID
     * @param input_json 输入数据 JSON
     * @param output_json 输出数据 JSON
     * @param metadata_json 元数据 JSON
     */
    void log_node_io(
        const std::string& strategy_name,
        int64_t epoch,
        const std::string& node_type,
        const std::string& node_id,
        const std::string& input_json,
        const std::string& output_json,
        const std::string& metadata_json
    );

    // ========== Tick 数据 ==========

    /**
     * 批量记录 Tick 数据（异步，立即返回，供 RecordHandler 调用）
     * @param ticks Tick 数据列表
     */
    void log_ticks(const std::vector<TickDataEntry>& ticks);

    /**
     * 查询 Tick 数据（同步，供 HTTP API 使用）
     * @param symbol 标的代码（空=全部）
     * @param start_ts 开始时间戳（epoch seconds）
     * @param end_ts 结束时间戳
     * @param limit 返回条数限制
     * @return Tick 数据列表，按时间升序
     */
    std::vector<TickDataEntry> query_ticks(
        const std::string& symbol,
        int64_t start_ts,
        int64_t end_ts,
        int limit = 10000
    );

    /**
     * 删除指定时间前的 Tick 数据
     * @return 删除的行数
     */
    int64_t delete_tick_data_before(int64_t timestamp_epoch);

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
     * 删除策略日志（按条件）
     * @param strategy_name 策略名称（空=全部）
     * @param level 日志级别（空=全部）
     * @param start_time 开始时间（空=无限制）
     * @param end_time 结束时间（空=无限制）
     * @return 删除的行数（负数表示失败）
     */
    struct DeleteResult {
        int64_t deleted_count;
        std::string error;
    };
    DeleteResult delete_strategy_logs(
        const std::string& strategy_name,
        const std::string& level,
        const std::string& start_time,
        const std::string& end_time
    );

    // ========== 节点输入输出日志 ==========

    /**
     * 查询节点 IO 日志（同步，供HTTP API使用）
     */
    std::vector<NodeIOEntry> query_node_io_logs(
        const std::string& strategy_name = "",
        const std::string& node_type = "",
        int64_t epoch_from = 0,
        int64_t epoch_to = 0,
        const std::string& start_time = "",
        const std::string& end_time = "",
        int limit = 1000,
        int offset = 0
    );

    /**
     * 查询符合条件的节点 IO 日志总数
     */
    int count_node_io_logs(
        const std::string& strategy_name = "",
        const std::string& node_type = "",
        int64_t epoch_from = 0,
        int64_t epoch_to = 0,
        const std::string& start_time = "",
        const std::string& end_time = ""
    );

    /**
     * 清理指定日期前的节点 IO 日志
     * @return 删除的行数
     */
    int64_t delete_node_io_logs_before(const std::string& timestamp);

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

    // 初始化 node_io_logs 表
    void init_node_io_table();

    // 初始化 tick_data 表
    void init_tick_table();

    // 后台写入线程主循环
    void worker_loop();

    // 批量写入DuckDB
    void batch_insert(const std::vector<StrategyLogEntry>& entries);

    // 批量写入节点 IO 日志
    void batch_insert_node_io(const std::vector<NodeIOEntry>& entries);

    // 批量写入 Tick 数据
    void batch_insert_ticks(const std::vector<TickDataEntry>& entries);

    // 执行 SQL（无参数）
    bool exec(const std::string& sql);

    // 检查表是否存在
    bool table_exists(const std::string& name);

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

    // 策略日志异步队列
    std::queue<StrategyLogEntry> queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // 节点 IO 日志异步队列
    std::queue<NodeIOEntry> node_io_queue_;
    std::mutex node_io_queue_mutex_;
    std::condition_variable node_io_queue_cv_;

    // Tick 数据异步队列
    std::queue<TickDataEntry> tick_queue_;
    std::mutex tick_queue_mutex_;
    std::condition_variable tick_queue_cv_;

    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::thread worker_thread_;

    // per-table ID counters (独立 atomic，无锁分配)
    std::atomic<uint64_t> next_strategy_log_id_{1};
    std::atomic<uint64_t> next_node_io_id_{1};
    std::atomic<uint64_t> next_tick_id_{1};

    // 批量写入配置
    static constexpr int BATCH_SIZE = 200;          // 批量阈值
    static constexpr int FLUSH_INTERVAL_MS = 500;   // 强制刷新间隔
};
