#pragma once
#include "HttpHandler.h"

/// 通用金融回归端点
///
/// POST /v0/analysis/regression
///   body: regression.md §1 的 spec（变体 A 面板直传）
///   200 → 与 Python 侧同 schema 的结果 JSON
///   400 → 请求不合法（维度不符、因子不存在、规模超限）
///   500 → 内核异常
///
/// 本类只做 httplib 薄包装：解析 body → 调 Util/Spec/regression_spec → 写响应。
/// 全部 spec 逻辑在 regression_spec 里，不在这里——这样它不依赖 httplib，
/// 可以被独立链接和单测。
class RegressionHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;

    /// GET 给出用法提示（避免调用方拿到一个空的 200）
    void get(const httplib::Request& req, httplib::Response& res) override;

    void post(const httplib::Request& req, httplib::Response& res) override;
};
