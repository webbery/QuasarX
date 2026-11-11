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

    /// 删除类目表里满足条件的行：symbol 为空表示不限标的，
    /// 日期范围留空表示不限日期（两者都空 = 清空全表，调用方应显式确认）。
    /// 表名必须是已注册的类目（白名单），日期必须是 YYYY-MM-DD。
    /// 返回删除行数；<0 表示参数非法或执行失败。
    int deleteRows(const String& table, const String& symbol,
                   const String& start_date, const String& end_date);

    /// 类目数据概览：总行数 / 标的数 / 日期范围 / 近 recent_days 个交易日每日行数。
    /// 用于前端首页展示（数据到哪天、每天多少行）。
    nlohmann::json queryCategoryOverview(const String& table, int recent_days = 30);

    /// 删除某次分红事件（按 symbol + ex_dividend_date 唯一定位）
    bool deleteDividendEvent(const String& symbol, const String& ex_date);

    int importDividendCsv(const String& csv_path);

    /// 批量导入分红目录下所有 CSV。
    /// symbols 非空时只导入名单内的标的（内部符号，如 "sh.600176"）。
    /// 目录里可能躺着历史上别的任务留下的 CSV；不设限就会把池外标的
    /// （如 sh.601398）的分红事件写进 dividend 表，并触发这些标的的
    /// 复权重算 —— 复权因子一旦算错，原始价就被放大，行情特征随之失真。
    ///
    /// 返回值是成功导入/更新的总行数（>= 0）；失败的文件数通过 failed_files 回传。
    /// 早期版本用负返回值表示"有文件失败"，但那样会把已成功导入的行数一并丢掉，
    /// 调用方无法区分"没有数据"（0）和"数据不完整"（部分成功）。
    int importAllDividends(const String& dividend_dir, const Set<String>& symbols = {},
                           int* failed_files = nullptr);

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
