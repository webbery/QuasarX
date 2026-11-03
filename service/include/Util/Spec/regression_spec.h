#pragma once

/// 回归 spec 编解码层
/// ────────────────────────────────────────────────────────────────────────────
/// 职责：JSON ⇄ PanelData 的转换 + spec 校验 + 响应序列化。
///
/// 分层意图（见 Algorithms/regression.h 顶部说明）：
///   本层依赖 json.hpp，内核层 Algorithms/regression.{h,cpp} **不依赖** JSON。
///   拆开的原因：M3 的图内节点（ResidualizeNode）只用 PanelData + 三个函数，
///   不该因为 REST 接口的需要而背上 926 KB / 25k 行的 json.hpp。
///
/// 只对外一套 wire format（regression.md §6.1）：
///   变体 A 面板直传：dates/symbols/y/x/f/groups + xnames/fnames/groupnames
///   变体 B 服务端取数：source.feature_cache + 特征名引用（M1 暂不实现）
///   两者差别只在"面板从哪来"，mode/se/options 完全一致。
///
/// 响应 schema 与 Python 侧（tools/ml_regression.py）逐字同构，
/// 因为 parity 测试是 M1 的验收标准，不是"以后再补"。

#include "Algorithms/regression.h"

#include "json.hpp"

#include <string>

namespace regression_spec {

/// 面板规模硬上限
///
/// httplib 线程池只有 hw/2 个 worker（server.cpp:329-338），同步计算会占满
/// 工作线程。面板直传天然受客户端手里的数据量约束，但服务端仍要兜底：
/// 超限直接 400 并说明，避免一个超大请求把整个 HTTP 服务拖死。
/// 超大规模分析走"先落缓存、再按 spec 跑"的离线路径。
struct Limits {
    int       _max_symbols = 300;        ///< 标的数 N
    int       _max_dates   = 5000;       ///< 日期数 T
    long long _max_cells   = 1500000;    ///< 单元格数 T×N（= 300 × 5000）
};

/// 错误分层：400 请求不合法（维度不符、特征找不到）/ 500 内核异常
struct SpecError {
    int         _status = 400;
    std::string _message;
};

/// 执行一份 spec
///
/// @param spec   请求体（regression.md §1 的 JSON 结构）
/// @param limits 规模上限
/// @param out    成功时写入响应 JSON（schema 与 Python 侧同构）
/// @param err    失败时写入 status + message
/// @return true = 成功；false = 失败（err 已填好）
///
/// 本函数不抛异常（内核异常被捕获成 500），便于 handler 直接调用。
bool Execute(const nlohmann::json& spec, const Limits& limits,
             nlohmann::json& out, SpecError& err);

/// 用默认上限执行（便于单测与 CLI 调试）
bool Execute(const nlohmann::json& spec, nlohmann::json& out, SpecError& err);

}  // namespace regression_spec
