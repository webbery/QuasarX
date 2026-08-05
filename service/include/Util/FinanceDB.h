#pragma once
#include "std_header.h"
#include "Util/DuckDBBaseT.h"
#include "json.hpp"
#include <mutex>

struct FinanceRow {
    std::string symbol;
    std::string stat_date;
    std::string pub_date;
    std::vector<std::pair<std::string, double>> fields;
};

class FinanceDB : public DuckDBBaseT<FinanceDB> {
public:
    static FinanceDB& instance();

    int importCsv(const std::string& csv_path, const std::string& category);

    nlohmann::json query(const std::string& category,
                         const std::string& symbol = "",
                         const std::string& start_date = "",
                         const std::string& end_date = "",
                         int limit = 500);

    std::vector<std::string> listTables();

    std::vector<std::string> listSymbols(const std::string& table);

    bool dropTable(const std::string& table);

    bool deleteSymbol(const std::string& table, const std::string& symbol);

    int importDividendCsv(const std::string& csv_path);

    int importAllDividends(const std::string& dividend_dir);

    nlohmann::json queryDividendByDate(const std::string& date);

    nlohmann::json queryDividendBySymbol(const std::string& symbol,
                                         const std::string& start_date = "",
                                         const std::string& end_date = "");

    struct DividendEvent {
        std::string symbol;
        time_t ex_dividend_date = 0;
        double cash_per_10 = 0;
        double bonus_per_10 = 0;
        double transfer_per_10 = 0;
        double ex_div_price = 0;
        int action_type = 0;

        nlohmann::json toJson() const;
    };

    static double calcEventAdjFactor(double prev_close, const DividendEvent& event);

    std::vector<DividendEvent> getDividendEvents(const std::string& symbol);

    int recalcSymbolAdjPrices(const std::string& symbol);

    nlohmann::json recalcAllAdjPrices();

    static int64_t encodeSymbol(const std::string& sym);
    static std::string decodeSymbol(int64_t encoded);

    static bool isValidCategory(const std::string& category);

    static const std::vector<std::pair<std::string, std::string>>&
    categoryFields(const std::string& category);

    static std::string categoryName(const std::string& category);

private:
    friend class DuckDBBaseT<FinanceDB>;
    void ensureTables();

    void ensureTable(const std::string& category);
    void ensureDividendTable();

public:
    ~FinanceDB();
};
