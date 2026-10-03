#pragma once
#include "std_header.h"
#include "KBarBuilder.h"

/// 缺失值填充策略
enum class FillMethod {
    None,           // 不对齐时直接跳过（截断到共同长度）
    ForwardFill,    // 前向填充：用上一个已知值填充
    BackwardFill,   // 后向填充：用下一个已知值填充
    Linear,         // 线性插值
    ZeroFill        // 零填充
};

/// 复权类型
enum class AdjType : int {
    HFQ = 0,   // 后复权（指标计算/分析用）
    None  = 1, // 不复权（原始价格/撮合用）
};

/// 解析填充策略字符串
FillMethod parseFillMethod(const String& str);

/// 字符串表示
String toString(FillMethod method);

/// 解析频率字符串到 BarFreq 枚举
BarFreq parseBarFreq(const String& str);

/// BarFreq 转字符串
String toString(BarFreq freq);

/// 从 CSV 数据文件加载多列时间序列数据（无频率自适应）
Map<String, Vector<double>> LoadHistoryData(
    const String& symbol,
    const Vector<String>& fields,
    const String& start_date = "",
    const String& end_date = "",
    Vector<String>* out_dates = nullptr,
    FillMethod fill = FillMethod::None);

/// 从 CSV 数据文件加载多列时间序列数据（带频率自适应）
/// 优先加载 target_freq 对应的 CSV，若不存在则查找可用频率并聚合
/// @param symbol     标的代码 (symbol_t 类型)
/// @param fields     字段名列表 (close/open/high/low/volume/turnover)
/// @param start_date 起始日期 "YYYY-MM-DD"，空字符串表示从最早开始
/// @param end_date   结束日期 "YYYY-MM-DD"，空字符串表示到最新
/// @param target_freq 目标频率（如 Day 表示日线），BarFreq::Min1 表示不限制频率
/// @param adj        复权类型，默认 HFQ（后复权）
/// @param out_dates  输出日期序列（可选）
/// @param fill       填充策略，默认 None（截断到共同长度）
/// @return           map: field → 数据序列（已按 fill 策略对齐，必要时聚合）
Map<String, Vector<double>> LoadHistoryDataWithFreq(
    const symbol_t& symbol,
    const Vector<String>& fields,
    const String& start_date,
    const String& end_date,
    BarFreq target_freq,
    AdjType adj = AdjType::HFQ,
    Vector<String>* out_dates = nullptr,
    FillMethod fill = FillMethod::None);

/// 将低频率数据聚合到高频率（如 5m → 1d）
struct ResampledData {
    Map<String, Vector<double>> data;
    Vector<String> dates;
};
ResampledData ResampleToFrequency(
    const Map<String, Vector<double>>& source_data,
    const Vector<String>& source_dates,
    BarFreq source_freq,
    BarFreq target_freq,
    const Vector<String>& fields);

/// === 宏观经济数据获取 ===

bool FetchMacroData(
    const String& symbol,
    const String& db_path,
    Vector<String>& out_dates,
    Vector<double>& out_prices);

/// === DuckDB 数据管理共享工具 ===

namespace DataUtil {

/// 将 CSV 行写入临时文件，返回文件路径
/// 目录: {temp}/quasarx_test/{name}.csv
String WriteTempCsv(const std::vector<String>& csv_lines,
                         const String& name);

/// 删除临时 CSV 文件
void CleanupTempFile(const String& path);

/// 通用清理模式：按 table/symbol 粒度删除
/// delete_symbol_fn: (table, symbol) -> bool
/// drop_table_fn:    (table) -> bool
/// list_tables_fn:   () -> vector<string>
struct DBCleanupOps {
    std::function<bool(const String& table, const String& symbol)> delete_symbol;
    std::function<bool(const String& table)> drop_table;
    std::function<std::vector<String>()> list_tables;
};

/// 执行清理，返回 {success, message}
std::pair<bool, String> CleanupDBData(
    const String& table,
    const String& symbol,
    const DBCleanupOps& ops);

} // namespace DataUtil

/// === BaoStock 标的下载 + 导入工具 ===

/// 单标的下载+导入结果
struct DownloadSymbolResult {
    int rows = 0;             ///< CSV 数据行数
    int imported = 0;         ///< QuoteDB 导入行数
    bool download_ok = false; ///< 下载脚本是否成功
    bool import_ok = false;   ///< CSV 是否成功导入 QuoteDB
};

/// 下载单个标的 (ETF/Stock) 并导入 QuoteDB（同步、无 SSE、无线程）
///
/// 流程: RunCommand(download_etf_bs.py) → 扫描 CSV → QuoteDB::importCsv → 删除 CSV
/// 自动从 symbol 推断 asset_type: sh.5xxxxx/sh.58xxxx → etf, 其余 → stock
///
/// @param symbol       baostock 格式 (如 "sh.510050", "sh.600519")
/// @param freq         频率 ("daily", "5m", "15m", "30m", "60m")
/// @param start/end    日期范围 "YYYY-MM-DD"，空 = 脚本默认值
/// @param interpreter  Python 解释器路径
/// @param quote_dir    QuoteDB 数据目录 (如 "{db_path}/quote")
/// @param overwrite    true=全量覆盖, false=增量 UPDATE/INSERT
DownloadSymbolResult DownloadAndImportSymbol(
    const String& symbol,
    const String& freq,
    const String& start,
    const String& end,
    const String& interpreter,
    const String& quote_dir,
    bool overwrite = false);

/// === 标的 → QuoteDB 查询工具 ===

/// 根据标的代码 + 交易所，解析 QuoteDB 内部 symbol 和所在表名
struct UnderlyingQuoteInfo {
    String symbol;       ///< QuoteDB 内部格式 (如 "sh.510050")
    String table;        ///< QuoteDB 表名 (如 "etf_1d" / "stock_1d")
    double latest_close; ///< 最新收盘价 (QuoteDB 未初始化或无数据时为 0)
};

/// 从期权 underlying 代码 + exchange 解析 QuoteDB 查询信息
///
/// 自动判断 asset_type: is_etf(to_symbol(sym)) → etf_1d, 否则 → stock_1d
/// 同时从 QuoteDB 取最新收盘价 (getLatestClose)
///
/// underlying 可能是产品名 (如 "50ETF") 而非实际代码 ("510050")，
/// 传入 product 可启用 product→code 映射 (SSE: 50ETF→510050 等)
///
/// @param underlying  标的代码或产品名 (如 "510050" / "50ETF" / "000300")
/// @param exchange    交易所 ("SSE" / "SZSE" / "CFFEX")
/// @param product     期权品种名 (如 "50ETF"/"IO"), 用于 underlying 是产品名时反查代码
UnderlyingQuoteInfo ResolveUnderlying(const String& underlying, const String& exchange,
                                      const String& product = "");
