#pragma once
#include "std_header.h"
#include "Util/data.h"  // AdjType
#include "Util/DuckDBBaseT.h"
#include <mutex>

struct QuoteBar {
    std::string symbol;       // "sh.510300"
    std::string datetime;     // "2026-07-03 09:35:00"
    double open = 0, close = 0, high = 0, low = 0;
    int64_t volume = 0;
    double turnover = 0;
    uint8_t ext = 0;
    double adj_open = 0, adj_close = 0, adj_high = 0, adj_low = 0;
};

class QuoteDB : public DuckDBBaseT<QuoteDB> {
public:
    static QuoteDB& instance();

    // 一次性导入不复权(org) + 后复权(hfq)两份 CSV，合并后删除原数据再批量列式插入（插入并替换）
    // 两份 CSV 可为同一文件（此时原始列与 adj 列同值）
    // 返回导入行数，失败返回 -1
    int importCsv(const std::string& org_csv_path,
                  const std::string& hfq_csv_path,
                  const std::string& table,
                  const std::string& symbol_str);

    bool upsertBar(const std::string& table, const QuoteBar& bar);

    std::vector<QuoteBar> query(const std::string& table,
                                const std::string& symbol,
                                const std::string& start_time = "",
                                const std::string& end_time = "",
                                int limit = 5000);

    std::vector<std::string> listTables();

    std::vector<std::string> listSymbols(const std::string& table);

    bool dropTable(const std::string& table);

    bool deleteSymbol(const std::string& table, const std::string& symbol);

    /// 删除单根 K 线（精确到日期/时刻），datetime 格式 YYYY-MM-DD 或 YYYY-MM-DD HH:MM:SS
    bool deleteBar(const std::string& table, const std::string& symbol, const std::string& datetime);

    struct SymbolTimeRange {
        symbol_t symbol;
        std::string start_time;
        std::string end_time;
        int64_t count;
    };
    std::vector<SymbolTimeRange> getSymbolTimeRanges(const std::string& table);

    static int64_t encodeSymbol(const std::string& sym);
    static std::string decodeSymbol(int64_t encoded);

    static std::string tableName(const std::string& asset_type, const std::string& freq);
    static std::string normalizeFreq(const std::string& freq);

    double getLatestClose(const std::string& table, const std::string& symbol_str);

    struct AdjPriceUpdate {
        std::string datetime;
        double adj_open, adj_close, adj_high, adj_low;
    };
    int updateAdjPrices(const std::string& table, int64_t encoded_symbol,
                        const std::vector<AdjPriceUpdate>& updates);

private:
    friend class DuckDBBaseT<QuoteDB>;
    void ensureTables();

    void ensureTable(const std::string& table);
};
