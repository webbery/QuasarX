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

    // ── dividend 表（分红除权数据）──
    //
    // CSV 格式（由 fetch_dividend_data.py 生成）:
    //   symbol, announce_date, report_year, ex_dividend_date, record_date,
    //   implement_date, bonus_per_10, transfer_per_10, cash_per_10,
    //   allot_per_10, allot_price, ex_div_price, action_type
    //
    // action_type: 0=未知 1=分红 2=送股 3=转增 4=送转 5=分红送转 6=配股 7=混合

    // 导入单个 {symbol}_dividend.csv，symbol 从 CSV 首列提取
    // 返回导入行数，失败返回 -1
    int importDividendCsv(const std::string& csv_path);

    // 扫描目录下所有 *_dividend.csv 文件，逐个调用 importDividendCsv
    // 返回总导入行数
    int importAllDividends(const std::string& dividend_dir);

    // 查询指定日期（YYYY-MM-DD）有除权除息事件的所有标的
    // 返回 JSON: {date, count, data: [{symbol, action_type, cash_per_10, ...}]}
    nlohmann::json queryDividendByDate(const std::string& date);

    // 查询指定标的在日期范围内的分红历史
    // 返回 JSON: {code, count, data: [{ex_dividend_date, action_type, ...}]}
    nlohmann::json queryDividendBySymbol(const std::string& symbol,
                                         const std::string& start_date = "",
                                         const std::string& end_date = "");

    // ── dividend 表驱动的后复权价格计算 ──
    //
    // 核心公式 (BaoStock 涨跌幅复权法):
    //   ex_div_ref_price = (前收盘价 − 每股现金分红 + 配股价 × 配股比例)
    //                     / (1 + 送股比例 + 配股比例)
    //   后复权因子(T) = Close(T-1) / ex_div_ref_price
    //   adj_factor(t) = ∏ [ 后复权因子(e) ] for all e where ex_date > t
    //   adj_price(t)  = org_price(t) × adj_factor(t)
    //
    // 数据来源:
    //   - dividend 表: ex_div_price (即除权除息参考价)
    //   - stock_1d 表: 原始 OHLC + 前一交易日收盘价

    struct DividendEvent {
        std::string symbol;
        time_t ex_dividend_date = 0;
        double cash_per_10 = 0;
        double bonus_per_10 = 0;
        double transfer_per_10 = 0;
        double ex_div_price = 0;       // 除权除息参考价 (= BaoStock preclose)
        int action_type = 0;

        nlohmann::json toJson() const;
    };

    // 计算单个分红事件的后复权因子
    //   factor = prev_close / ex_div_price
    //   当 ex_div_price 缺失时使用公式反推:
    //     ex_div_price ≈ prev_close * (1 - cash/10/prev_close + (bonus+transfer)/10)
    static double calcEventAdjFactor(double prev_close, const DividendEvent& event);

    // 查询指定标的的所有分红事件 (按 ex_date 升序)
    std::vector<DividendEvent> getDividendEvents(const std::string& symbol);

    // 重算指定标的的 stock_1d 后复权价格，写回 adj_open/adj_high/adj_low/adj_close
    // 返回处理的 bar 数量，-1 表示错误
    int recalcSymbolAdjPrices(const std::string& symbol);

    // 批量重算所有标的的后复权价格
    // 返回 JSON: {symbols, bars, errors}
    nlohmann::json recalcAllAdjPrices();

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
    void ensureDividendTable();
    bool exec(const std::string& sql);

    duckdb_database db_ = nullptr;
    duckdb_connection conn_ = nullptr;
    bool initialized_ = false;
    std::recursive_mutex mtx_;
};
