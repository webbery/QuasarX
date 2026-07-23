#pragma once
#include "std_header.h"
#include "Util/data.h"  // AdjType
#include "duckdb.h"
#include <mutex>

struct QuoteBar {
    std::string symbol;       // "sh.510300"
    std::string datetime;     // "2026-07-03 09:35:00"
    // 不复权价格（撮合用）
    double open = 0, close = 0, high = 0, low = 0;
    int64_t volume = 0;
    double turnover = 0;
    uint8_t ext = 0;
    // 后复权价格（指标计算用）
    double adj_open = 0, adj_close = 0, adj_high = 0, adj_low = 0;
};

class QuoteDB {
public:
    static QuoteDB& instance();

    bool init(const std::string& db_dir);
    bool isInitialized() const { return initialized_; }
    void shutdown();

    // CSV 导入到指定表，返回导入行数
    int importCsv(const std::string& csv_path,
                  const std::string& table,
                  const std::string& symbol_str,
                  AdjType adj = AdjType::HFQ);

    // 插入或更新单根 bar（用于实时收盘数据写入）
    // 如果 (symbol, datetime) 已存在则更新，否则插入
    bool upsertBar(const std::string& table, const QuoteBar& bar);

    // 查询行情
    std::vector<QuoteBar> query(const std::string& table,
                                const std::string& symbol,
                                const std::string& start_time = "",
                                const std::string& end_time = "",
                                int limit = 5000);

    // 列出所有行情表
    std::vector<std::string> listTables();

    // 获取表中所有 symbol（返回字符串格式）
    std::vector<std::string> listSymbols(const std::string& table);

    // 删除表
    bool dropTable(const std::string& table);

    // 删除指定标的（某表内的某 symbol），返回是否成功
    bool deleteSymbol(const std::string& table, const std::string& symbol);

    // 查询某 symbol 的时间范围
    struct SymbolTimeRange {
        symbol_t symbol;         // symbol_t 类型（内部编码）
        std::string start_time;  // MIN(datetime)
        std::string end_time;    // MAX(datetime)
        int64_t count;           // COUNT(*)
    };
    std::vector<SymbolTimeRange> getSymbolTimeRanges(const std::string& table);

    // symbol 编解码
    static int64_t encodeSymbol(const std::string& sym);
    static std::string decodeSymbol(int64_t encoded);

    // 表名生成: asset_type + freq → "etf_5m"
    static std::string tableName(const std::string& asset_type, const std::string& freq);

    // 频率标准化: "daily" → "1d"
    static std::string normalizeFreq(const std::string& freq);

    // 批量更新复权价格（由 FinanceDB 调用，通过 QuoteDB 自身连接写入）
    struct AdjPriceUpdate {
        std::string datetime;  // "2024-01-02 00:00:00"
        double adj_open, adj_close, adj_high, adj_low;
    };
    int updateAdjPrices(const std::string& table, int64_t encoded_symbol,
                        const std::vector<AdjPriceUpdate>& updates);

private:
    QuoteDB() = default;
    ~QuoteDB();

    void ensureTable(const std::string& table);
    bool exec(const std::string& sql);

    duckdb_database db_ = nullptr;
    duckdb_connection conn_ = nullptr;
    bool initialized_ = false;
    std::mutex mtx_;
};
