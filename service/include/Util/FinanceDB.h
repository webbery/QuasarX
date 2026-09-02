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

    struct DividendEvent {
        String symbol;
        time_t ex_dividend_date = 0;
        double cash_per_10 = 0;
        double bonus_per_10 = 0;
        double transfer_per_10 = 0;
        double ex_div_price = 0;
        int action_type = 0;

        nlohmann::json toJson() const;
    };

    static double calcEventAdjFactor(double prev_close, const DividendEvent& event);

    Vector<DividendEvent> getDividendEvents(const String& symbol);

    int recalcSymbolAdjPrices(const String& symbol);

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

public:
    ~FinanceDB();
};
