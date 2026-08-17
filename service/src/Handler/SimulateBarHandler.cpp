#ifdef _DEBUG

#include "Handler/SimulateBarHandler.h"
#include "server.h"
#include "StrategySubSystem.h"
#include "Util/QuoteDB.h"
#include "Util/FinanceDB.h"
#include "Util/datetime.h"

SimulateBarHandler::SimulateBarHandler(Server* server)
    : HttpHandler(server) {
}

SimulateBarHandler::~SimulateBarHandler() {
}

void SimulateBarHandler::post(const httplib::Request& req, httplib::Response& res) {
    try {
        auto params = nlohmann::json::parse(req.body);

        // ── 解析必填字段 ──
        if (!params.contains("symbol") || !params.contains("close")) {
            res.status = 400;
            res.set_content(R"({"error": "symbol and close are required"})", "application/json");
            return;
        }

        String symbol = params["symbol"];

        QuoteBar bar;
        bar.symbol = symbol;
        bar.close = params.value("close", 0.0);
        bar.open = params.value("open", bar.close);
        bar.high = params.value("high", bar.close);
        bar.low = params.value("low", bar.close);
        bar.volume = params.value("volume", (int64_t)0);
        bar.turnover = params.value("turnover", 0.0);

        // datetime 默认当天
        if (params.contains("datetime")) {
            bar.datetime = (String)params["datetime"];
        } else {
            bar.datetime = ToString(Now(), "%Y-%m-%d") + " 00:00:00";
        }

        // ── 1. 写入 QuoteDB ──
        auto& quoteDB = QuoteDB::instance();
        if (!quoteDB.isInitialized()) {
            res.status = 500;
            res.set_content(R"({"error": "QuoteDB not initialized"})", "application/json");
            return;
        }

        if (!quoteDB.upsertBar("stock_1d", bar)) {
            res.status = 500;
            res.set_content(R"({"error": "Failed to write bar to QuoteDB"})", "application/json");
            return;
        }

        // ── 2. 后复权计算 ──
        auto& financeDB = FinanceDB::instance();
        if (financeDB.isInitialized()) {
            financeDB.recalcSymbolAdjPrices(symbol);
        }

        // ── 3. 触发策略管道 ──
        auto* strategySys = _server->GetStrategySystem();
        if (strategySys) {
            INFO("[SimulateBar] Calling EnsureDailyReady + MarkSymbolReady for {}", symbol);
            strategySys->EnsureDailyReady();
            // 从 bar datetime 提取日期，日期变化时 ResetDaily 使策略可重新执行
            String simDate = bar.datetime.substr(0, 10);
            time_t simTs = FromStr(simDate, "%Y-%m-%d");
            if (simTs != _lastSimDate) {
                strategySys->ResetDaily();
                _lastSimDate = simTs;
            }
            strategySys->MarkSymbolReady(symbol, simDate);
        } else {
            WARN("[SimulateBar] StrategySubSystem is null!");
        }

        // ── 返回 ──
        nlohmann::json response;
        response["status"] = "ok";
        response["symbol"] = symbol;
        response["datetime"] = bar.datetime;
        response["close"] = bar.close;
        response["message"] = "Bar written and symbol marked ready. Strategy will execute when all dependencies are met.";

        INFO("[SimulateBar] {} close={} at {}", symbol, bar.close, bar.datetime);

        res.status = 200;
        res.set_content(response.dump(2), "application/json");

    } catch (const std::exception& e) {
        res.status = 400;
        nlohmann::json error;
        error["error"] = e.what();
        res.set_content(error.dump(), "application/json");
    }
}

#endif // _DEBUG
