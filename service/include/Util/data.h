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
