#include "Handler/StrategyTradeHandler.h"
#include "Util/DecisionDB.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Util/string_algorithm.h"
#include <map>
#include <queue>
#include <cmath>

static std::string formatTimestamp(time_t ts) {
    struct tm tm_val;
#ifdef _WIN32
    gmtime_s(&tm_val, &ts);
#else
    gmtime_r(&ts, &tm_val);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_val);
    return buf;
}

struct BuyLot {
    time_t date;
    double price;
    int64_t quantity;
};

void StrategyTradeHandler::get(const httplib::Request& req, httplib::Response& res) {
    String strategy = req.get_param_value("name");
    if (strategy.empty()) {
        res.status = 400;
        res.set_content(R"({"error": "missing 'name' parameter"})", "application/json");
        return;
    }

    time_t startDate = 0, endDate = 0;
    String startStr = req.get_param_value("start");
    String endStr = req.get_param_value("end");
    if (!startStr.empty()) startDate = FromStr(startStr, "%Y-%m-%d");
    if (!endStr.empty()) endDate = FromStr(endStr, "%Y-%m-%d");

    auto records = DecisionDB::instance().queryDailyPositions(strategy, startDate, endDate);
    if (records.empty()) {
        nlohmann::json result;
        result["strategy"] = strategy;
        result["trades"] = nlohmann::json::array();
        result["holdings"] = nlohmann::json::array();
        result["message"] = "no position data found";
        res.set_content(result.dump(), "application/json");
        return;
    }

    // Group by symbol, preserving date order
    std::map<symbol_t, std::vector<const DailyPositionRecord*>> bySymbol;
    for (auto& rec : records) {
        bySymbol[rec.symbol].push_back(&rec);
    }

    nlohmann::json trades = nlohmann::json::array();
    nlohmann::json holdings = nlohmann::json::array();
    int winCount = 0, lossCount = 0;
    double totalPnl = 0.0;

    for (auto& [sym, symRecords] : bySymbol) {
        // Sort by date (should already be sorted from SQL, but ensure)
        std::sort(symRecords.begin(), symRecords.end(),
            [](const DailyPositionRecord* a, const DailyPositionRecord* b) {
                return a->date < b->date;
            });

        String symStr = get_symbol(sym);
        std::queue<BuyLot> buyQueue;
        int64_t prevPosition = 0;

        for (auto* rec : symRecords) {
            int64_t delta = rec->position - prevPosition;
            prevPosition = rec->position;

            if (delta == 0) continue;

            if (delta > 0) {
                // Buy
                nlohmann::json trade;
                trade["date"] = formatTimestamp(rec->date);
                trade["symbol"] = to_utf8(symStr.c_str());
                trade["direction"] = "buy";
                trade["quantity"] = delta;
                trade["price"] = rec->close_price;
                trade["position_after"] = rec->position;
                trades.push_back(trade);

                buyQueue.push({rec->date, rec->close_price, delta});
            } else {
                // Sell
                int64_t sellQty = -delta;
                double sellPnl = 0.0;
                int64_t matchedQty = 0;

                while (sellQty > 0 && !buyQueue.empty()) {
                    auto& front = buyQueue.front();
                    int64_t matchQty = std::min(front.quantity, sellQty);
                    double pnl = (rec->close_price - front.price) * matchQty;
                    sellPnl += pnl;
                    matchedQty += matchQty;

                    // Emit holding record
                    nlohmann::json holding;
                    holding["symbol"] = to_utf8(symStr.c_str());
                    holding["buy_date"] = formatTimestamp(front.date);
                    holding["sell_date"] = formatTimestamp(rec->date);
                    holding["buy_price"] = front.price;
                    holding["sell_price"] = rec->close_price;
                    holding["quantity"] = matchQty;
                    int64_t holdDays = (rec->date - front.date) / 86400;
                    holding["holding_days"] = holdDays > 0 ? holdDays : 1;
                    holding["pnl"] = std::round(pnl * 100.0) / 100.0;
                    double pnlPct = front.price > 0 ? (rec->close_price / front.price - 1.0) * 100.0 : 0.0;
                    holding["pnl_pct"] = std::round(pnlPct * 100.0) / 100.0;
                    holdings.push_back(holding);

                    front.quantity -= matchQty;
                    sellQty -= matchQty;
                    if (front.quantity == 0) buyQueue.pop();
                }

                nlohmann::json trade;
                trade["date"] = formatTimestamp(rec->date);
                trade["symbol"] = to_utf8(symStr.c_str());
                trade["direction"] = "sell";
                trade["quantity"] = -delta;
                trade["price"] = rec->close_price;
                trade["position_after"] = rec->position;
                if (matchedQty > 0) {
                    trade["pnl"] = std::round(sellPnl * 100.0) / 100.0;
                }
                trades.push_back(trade);
            }
        }

        // Open positions (unmatched buys)
        while (!buyQueue.empty()) {
            auto& front = buyQueue.front();
            if (front.quantity > 0) {
                nlohmann::json holding;
                holding["symbol"] = to_utf8(symStr.c_str());
                holding["buy_date"] = formatTimestamp(front.date);
                holding["sell_date"] = nullptr;
                holding["buy_price"] = front.price;
                holding["sell_price"] = nullptr;
                holding["quantity"] = front.quantity;
                holding["holding_days"] = nullptr;
                holding["pnl"] = nullptr;
                holding["pnl_pct"] = nullptr;
                holding["open"] = true;
                holdings.push_back(holding);
            }
            buyQueue.pop();
        }
    }

    // Sort trades by date
    std::sort(trades.begin(), trades.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return a["date"].get<std::string>() < b["date"].get<std::string>();
    });

    // Compute summary from completed holdings (non-open)
    for (auto& h : holdings) {
        if (h.contains("pnl") && !h["pnl"].is_null()) {
            double pnl = h["pnl"].get<double>();
            totalPnl += pnl;
            if (pnl > 0) winCount++;
            else lossCount++;
        }
    }

    int totalClosed = winCount + lossCount;

    nlohmann::json summary;
    summary["total_trades"] = (int)trades.size();
    summary["completed_holdings"] = totalClosed;
    summary["win_count"] = winCount;
    summary["loss_count"] = lossCount;
    summary["win_rate"] = totalClosed > 0 ? std::round((double)winCount / totalClosed * 10000.0) / 100.0 : 0.0;
    summary["total_pnl"] = std::round(totalPnl * 100.0) / 100.0;
    summary["avg_pnl"] = totalClosed > 0 ? std::round(totalPnl / totalClosed * 100.0) / 100.0 : 0.0;

    nlohmann::json result;
    result["strategy"] = strategy;
    result["trading_days"] = (int)records.size();
    result["trades"] = std::move(trades);
    result["holdings"] = std::move(holdings);
    result["summary"] = std::move(summary);

    res.set_content(result.dump(2), "application/json");
}
