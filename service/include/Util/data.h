#pragma once
#include "std_header.h"

/// 缺失值填充策略
enum class FillMethod {
    None,           // 不对齐时直接跳过（截断到共同长度）
    ForwardFill,    // 前向填充：用上一个已知值填充
    BackwardFill,   // 后向填充：用下一个已知值填充
    Linear,         // 线性插值
    ZeroFill        // 零填充
};

/// 解析填充策略字符串
FillMethod parseFillMethod(const String& str);

/// 字符串表示
String toString(FillMethod method);

/// 从 CSV 数据文件加载多列时间序列数据
/// @param symbol     标的代码 (如 "sz.000001")
/// @param fields     字段名列表 (close/open/high/low/volume/turnover)
/// @param start_date 起始日期 "YYYY-MM-DD"，空字符串表示从最早开始
/// @param end_date   结束日期 "YYYY-MM-DD"，空字符串表示到最新
/// @param out_dates  输出日期序列（可选）
/// @param fill       填充策略，默认 None（截断到共同长度）
/// @return           map: field → 数据序列（已按 fill 策略对齐）
Map<String, Vector<double>> LoadHistoryData(
    const String& symbol,
    const Vector<String>& fields,
    const String& start_date = "",
    const String& end_date = "",
    Vector<String>* out_dates = nullptr,
    FillMethod fill = FillMethod::None);
