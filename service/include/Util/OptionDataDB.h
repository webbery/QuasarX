#pragma once
#include "std_header.h"
#include "Util/DuckDBBaseT.h"
#include "Bridge/OptionSymbolMacros.h"
#include "json.hpp"
#include <mutex>

// 期权日终数据 DuckDB 单例
// 数据源: CFFEX / SSE / SZSE 官方日终行情（通过 tools/fetch_*_option.py 下载）
// 表: option_daily
//
// 设计要点:
//   - symbol_id BIGINT 取代原 contract_code VARCHAR (8 字节 vs ~20 字节/行, 节省 50%+)
//   - symbol_id 用 packed symbol_t 编码合约 (类型+交易所+年月+strike+scale)
//   - contract_name VARCHAR 保留供前端展示 ("IO2401-C-3800")
//   - strike_price DOUBLE 显式存, 不只靠 symbol_t 解码 (避免解码歧义, 保留原始精度)
class OptionDataDB : public DuckDBBaseT<OptionDataDB> {
public:
    static OptionDataDB& instance();

    // 从 fetch_*_option.py 输出的 CSV 文件导入
    // CSV 格式（带 BOM, UTF-8-sig）:
    //   trade_date,product,contract_code,contract_name,call_put,strike_price,
    //   open,high,low,close,settlement,prev_settlement,
    //   volume,turnover,open_interest,exercise_volume,
    //   delta,gamma,vega,theta,implied_volatility
    // exchange 取自 csv_path 路径中的子目录名 ("cffex"/"sse"/"szse")
    int importCsv(const String& csv_path);

    // 查询某合约的历史日终数据
    // contract_code: 既支持原始字符串 "IO2401-C-3800", 也支持 hex 形式的 symbol_t
    nlohmann::json queryByContract(const String& contract_code,
                                   const String& start_date = "",
                                   const String& end_date = "",
                                   int limit = 500);

    // 按 symbol_id 直接查询历史日终数据 (前端列表已有 symbol_id, 无需再编码)
    nlohmann::json queryBySymbolId(int64_t symbol_id,
                                   const String& start_date = "",
                                   const String& end_date = "",
                                   int limit = 500);

    // 列出已下载合约概要
    // 返回: [{symbol_id, exchange, product, contract_name, call_put,
    //         strike_price, start_date, end_date, count}, ...]
    nlohmann::json listContracts(const String& exchange_filter = "",
                                 const String& product_filter = "");

    // 删除某个合约的所有数据 (按 symbol_id)
    bool deleteContract(const String& contract_code);

    // 删除所有期权日终数据
    bool deleteAll();

    // 工具: 合约字符串 → symbol_t (含 strike, year, month, type, scale 编码)
    static symbol_t encodeContract(const String& exchange,
                                   const String& contract_code,
                                   const String& contract_name = "",
                                   double strike = 0.0);

    // 工具: 合约字符串 → symbol_id (int64_t, 用于 DuckDB BIGINT)
    static int64_t encodeContractId(const String& exchange,
                                    const String& contract_code,
                                    const String& contract_name = "",
                                    double strike = 0.0);

private:
    friend class DuckDBBaseT<OptionDataDB>;
    void ensureTables();

public:
    ~OptionDataDB();
};
