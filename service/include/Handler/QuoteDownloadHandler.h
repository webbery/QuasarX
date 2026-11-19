#pragma once
#include "HttpHandler.h"
#include "Util/QuoteDB.h"
#include "nng/nng.h"
#include <string>
#include <vector>

/// 行情下载接口
///
/// POST /v0/quote 只做参数解析，实际下载/体检/导入交给
/// Server::SubmitQuoteDownload → Data/QuoteData 这条公共链路：
///   下载 → CSV 级体检（表头/OHLC/日期连续性）→ 分块入库 → DB 级复查 → 补数据
///
/// 这样定时任务和 REST 触发走的是同一套逻辑，不会出现「接口能导进去、
/// 定时任务导不进去」这类分叉。
class QuoteDownloadHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void get(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;

    /**
     * @brief 停止下载线程池（服务退出前调用）
     *
     * 先停止接收新任务，再等在跑的下载收尾，避免 DuckDB 关闭后后台 python
     * 还在写库。
     */
    static void ShutdownDownloads();
};
