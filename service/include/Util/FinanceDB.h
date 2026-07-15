#pragma once
#include "std_header.h"
#include "duckdb.h"
#include "json.hpp"
#include <mutex>

struct FinanceRow {
    std::string symbol;
    std::string stat_date;
    std::string pub_date;
    std::vector<std::pair<std::string, double>> fields;  // column_name -> value
};

class FinanceDB {
public:
    static FinanceDB& instance();

    bool init(const std::string& db_dir);
    bool isInitialized() const { return initialized_; }
    void shutdown();

    // CSV 导入到指定类别表，返回导入行数
    int importCsv(const std::string& csv_path, const std::string& category);

    // 查询财务数据，返回 JSON
    nlohmann::json query(const std::string& category,
                         const std::string& symbol = "",
                         const std::string& start_date = "",
                         const std::string& end_date = "",
                         int limit = 500);

    // 列出所有财务表
    std::vector<std::string> listTables();

    // 获取表中所有 symbol
    std::vector<std::string> listSymbols(const std::string& table);

    // 删除表
    bool dropTable(const std::string& table);

    // 删除指定标的
    bool deleteSymbol(const std::string& table, const std::string& symbol);

    // symbol 编解码（复用 QuoteDB 模式）
    static int64_t encodeSymbol(const std::string& sym);
    static std::string decodeSymbol(int64_t encoded);

    // 类别合法性检查
    static bool isValidCategory(const std::string& category);

    // 获取类别的字段定义 (column_name, display_name)
    static const std::vector<std::pair<std::string, std::string>>&
    categoryFields(const std::string& category);

    // 类别中文名
    static std::string categoryName(const std::string& category);

private:
    FinanceDB() = default;
    ~FinanceDB();

    void ensureTable(const std::string& category);
    bool exec(const std::string& sql);

    duckdb_database db_ = nullptr;
    duckdb_connection conn_ = nullptr;
    bool initialized_ = false;
    std::mutex mtx_;
};
