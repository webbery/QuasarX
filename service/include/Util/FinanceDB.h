#pragma once
#include "std_header.h"
#include "Util/DuckDBBaseT.h"
#include "json.hpp"
#include <mutex>

struct FinanceRow {
    String symbol;
    String stat_date;
    String pub_date;
    Vector<std::pair<String, double>> fields;
};

class FinanceDB : public DuckDBBaseT<FinanceDB> {
public:
    static FinanceDB& instance();

    int importCsv(const String& csv_path, const String& category);

    nlohmann::json query(const String& category,
                         const String& symbol = "",
                         const String& start_date = "",
                         const String& end_date = "",
                         int limit = 500);

    Vector<String> listTables();

    Vector<String> listSymbols(const String& table);

    bool dropTable(const String& table);

    bool deleteSymbol(const String& table, const String& symbol);

    /// 删除某次分红事件（按 symbol + ex_dividend_date 唯一定位）
    bool deleteDividendEvent(const String& symbol, const String& ex_date);

    int importDividendCsv(const String& csv_path);

    int importAllDividends(const String& dividend_dir);

    nlohmann::json queryDividendByDate(const String& date);

    nlohmann::json queryDividendBySymbol(const String& symbol,
                                         const String& start_date = "",
                                         const String& end_date = "");

    nlohmann::json queryAllDividends(int limit, int offset);

    // === 宏观数据相关 ===
    
    /// 导入宏观数据（从 JSON 格式）
    int importMacroJson(const String& country, 
                        const String& indicator,
                        const nlohmann::json& data);
    
    /// 查询宏观数据
    nlohmann::json queryMacro(const String& country,
                              const String& indicator,
                              const String& start_date = "",
                              const String& end_date = "",
                              int limit = 500);
    
    /// 列出所有宏观数据（按 country + indicator 分组）
    nlohmann::json listMacroData();
    
    /// 删除宏观数据
    bool deleteMacroData(const String& country, const String& indicator);
    
    /// 获取宏观数据的最新更新时间
    nlohmann::json queryMacroMetadata(const String& country, const String& indicator);

    // === 可做空标的（融资融券标的）===

    /// 导入可做空标的列表
    /// data 格式: {"data": [{"code": "sh.600000", "exchange": "SSE"}, ...]}
    int importMarginStocks(const nlohmann::json& data);

    /// 查询所有可做空标的（可按 exchange 过滤）
    nlohmann::json queryMarginStocks(const String& exchange = "");

    /// 查询指定标的是否可做空
    bool isMarginStock(const String& symbol);

    struct DividendEvent {
        String symbol;
        time_t ex_dividend_date = 0;
        double cash_per_10 = 0;
        double bonus_per_10 = 0;
        double transfer_per_10 = 0;
        double allot_per_10 = 0;
        double allot_price = 0;
        double ex_div_price = 0;
        int action_type = 0;

        nlohmann::json toJson() const;
    };

    static double calcEventAdjFactor(double prev_close, const DividendEvent& event);

    Vector<DividendEvent> getDividendEvents(const String& symbol);

    int recalcSymbolAdjPrices(const String& symbol, const Vector<DividendEvent>& events);

    nlohmann::json recalcAllAdjPrices();

    static int64_t encodeSymbol(const String& sym);
    static String decodeSymbol(int64_t encoded);

    static bool isValidCategory(const String& category);

    static const Vector<std::pair<String, String>>&
    categoryFields(const String& category);

    static String categoryName(const String& category);

private:
    friend class DuckDBBaseT<FinanceDB>;
    void ensureTables();

    void ensureTable(const String& category);
    void ensureDividendTable();
    void ensureMacroTable();
    void ensureMacroMetadataTable();
    void ensureMarginStockTable();

public:
    ~FinanceDB();
};
