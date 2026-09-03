#pragma once
#include "HttpHandler.h"
#include "Util/QuoteDB.h"
#include "nng/nng.h"
#include <string>
#include <vector>

class QuoteDownloadHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void get(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;

    struct DownloadGroup {
        std::string asset_type;          // "stock" / "etf"
        std::string symbols_str;         // 逗号分隔，外部格式（600000.SH）
        std::vector<std::string> symbols;
    };

    /**
     * @brief 复用 POST 的核心下载 + 导入逻辑（不通过 HTTP，直接传入参数）
     *
     * 按 (asset_type) 异步启动线程：
     *   1. RunCommand 下载脚本（同时下 HFQ + 不复权 CSV 到 quote/{asset_type}_{hfq,org}/{freq}）
     *   2. 扫描落盘 CSV 并调 QuoteDB::importCsv 入库
     *   3. 对 stock_1d 调用 validateAndReconcileAdj 校验下载 vs 重算
     *   4. 推送 SSE 进度到 sseSock
     *
     * 单 symbol 失败由脚本内部 try/except 捕获，整体 RunCommand 返回 ok=false 才视为组级失败。
     */
    static void runDownloadJob(nng_socket sseSock,
                               std::vector<DownloadGroup> groups,
                               const std::string& freq,
                               const std::string& start,
                               const std::string& end,
                               const std::string& env_name,
                               const std::string& quote_dir,
                               bool overwrite = false);

    /**
     * @brief 股票除权校验：对比下载 HFQ 与本地重算，差异>1% 则用下载值
     *
     * 仅 stock_1d 触发（ETF 不参与 A 股除权）。
     * 流程：读下载 adj_close → FinanceDB::recalcSymbolAdjPrices → 读重算 adj_close →
     *       比较两者，相对差异>1% 且下载值正常时通过 updateAdjPrices 恢复下载值。
     */
    static void validateAndReconcileAdj(nng_socket sseSock,
                                        const std::string& table,
                                        const std::string& symbol);

    /**
     * @brief 把下载的 adj_* 写回 stock_1d 单根 bar
     */
    static void restoreDownloadedAdj(const std::string& table,
                                     int64_t encoded_symbol,
                                     const QuoteBar& downloaded);
};
