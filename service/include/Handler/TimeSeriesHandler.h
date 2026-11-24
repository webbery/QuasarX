#pragma once
#include "HttpHandler.h"
#include "Util/finance.h"

/**
 * 时间序列计量分析 Handler
 *
 * 路由:
 *   POST /v0/analysis/time_series   （JSON body，支持直接传入时间序列）
 *   GET  /v0/analysis/time_series   （query 参数，只能按标的 + ohlcv 域取数）
 *
 * action 取值:
 *   - adf                ADF 单位根检验 (MacKinnon 临界值)
 *   - kpss               KPSS 平稳性检验
 *   - cointegration      Engle-Granger 两步法协整检验
 *   - johansen_trace     Johansen 迹检验
 *   - johansen_max_eigen Johansen 最大特征值检验
 *   - granger            Granger 因果检验
 *
 * 数据来源二选一:
 *   - series:  直接传入时间序列（二维数组），symbols 可用于给各序列命名
 *   - symbols: 按标的取数，field 指定 ohlcv 域（close/open/high/low/volume/turnover）
 *
 * adf / kpss 支持 on=residuals，让检验作用在协整回归残差上（EG 两步法的第二步）。
 *
 * 本类只负责取数、命名与 JSON 序列化；检验编排在 Util/TimeSeries，统计内核在 Util/finance。
 */
class TimeSeriesHandler : public HttpHandler {
public:
    TimeSeriesHandler(Server* server) : HttpHandler(server) {}

    virtual void get(const httplib::Request& req, httplib::Response& res);
    virtual void post(const httplib::Request& req, httplib::Response& res);

private:
    /// GET / POST 归一化后走同一条主流程
    void handle(const nlohmann::json& params, httplib::Response& res);
};
