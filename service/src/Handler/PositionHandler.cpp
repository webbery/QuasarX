#include "Handler/PositionHandler.h"
#include "ExchangeManager.h"
#include "BrokerSubSystem.h"
#include "server.h"
#include "Util/string_algorithm.h"

namespace {

// 交易所通道侧持仓（HX）。纯行情模式下没有该交易所，返回空数组。
nlohmann::json ExchangePositionsToJson(Server* server) {
    nlohmann::json positions = nlohmann::json::array();
    auto exchange = server->GetExchangeManager()->GetExchangeByType(ExchangeType::EX_HX);
    if (!exchange) return positions;

    AccountPosition account;
    if (!exchange->GetPosition(account)) return positions;

    for (auto& item : account._positions) {
        positions.push_back({
            {"id", get_symbol(item._symbol)},
            {"name", to_utf8(item._name)},
            {"price", item._avgPrice},
            {"curPrice", item._currentPrice},
            {"quantity", item._quantity},
            {"valid_quantity", item._validQuantity},
        });
    }
    return positions;
}

nlohmann::json CapitalToJson(const LedgerStore& ledger) {
    return {
        {"initialCapital", ledger.GetInitialCapital()},
        {"totalAllocated", ledger.GetTotalAllocated()},
        {"totalAvailable", ledger.GetTotalAvailable()},
        {"activeStrategies", ledger.GetActiveStrategyCount()},
    };
}

// 单个策略的账本条目
nlohmann::json LedgerAccountToJson(const LedgerStore::Account& account) {
    double positionValue = 0.0;
    nlohmann::json positions = nlohmann::json::array();
    for (const auto& [symbol, pos] : account._positions) {
        const double marketValue = pos.getMarketValue();
        positionValue += marketValue;
        positions.push_back({
            {"symbol", get_symbol(symbol)},
            {"quantity", pos._quantity},
            {"avgCost", pos._avgPrice},
            {"currentPrice", pos._currentPrice},
            {"marketValue", marketValue},
            {"isMargin", pos._isMargin},
            {"marginRatio", pos._marginRatio},
        });
    }

    nlohmann::json lots = nlohmann::json::object();
    for (const auto& [symbol, batch] : account._lots) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& lot : batch) {
            arr.push_back({
                {"quantity", lot._quantity},
                {"price", lot._price},
                {"date", static_cast<int64_t>(lot._date)},
            });
        }
        lots[get_symbol(symbol)] = std::move(arr);
    }

    return {
        {"allocated", account._allocated},
        {"cash", account._cash},
        {"borrowedCash", account._credit.borrowedCash},
        {"profit", account._profit},
        {"active", account._active},
        {"positionValue", positionValue},
        {"equity", account._cash + positionValue - account._credit.borrowedCash},
        {"positions", std::move(positions)},
        {"lots", std::move(lots)},
        {"credit", {
            {"borrowedCash", account._credit.borrowedCash},
            {"borrowedShares", account._credit.borrowedShares},
            {"shortAvgPrice", account._credit.shortAvgPrice},
            {"financedAmount", account._credit.financedAmount},
        }},
    };
}

nlohmann::json CreditToJson(const StrategyCreditState& credit) {
    return {
        {"borrowedCash", credit.borrowedCash},
        {"borrowedShares", credit.borrowedShares},
        {"shortAvgPrice", credit.shortAvgPrice},
        {"financedAmount", credit.financedAmount},
    };
}

}  // namespace

PositionHandler::PositionHandler(Server* server):HttpHandler(server) {

}

void PositionHandler::get(const httplib::Request& req, httplib::Response& res) {
    const String action = req.has_param("action") ? req.get_param_value("action") : String();
    const String only = req.has_param("strategy") ? req.get_param_value("strategy") : String();

    nlohmann::json result = nlohmann::json::object();
    auto* broker = _server->GetBrokerSubSystem();
    auto* ledger = broker ? broker->GetLedger() : nullptr;

    // 缺省：持仓 + 资金汇总 + 账本
    const bool wantAll = action.empty();
    const bool wantPositions = wantAll || action == "positions";
    const bool wantCapital = wantAll || action == "capital";
    const bool wantLedger = wantAll || action == "ledger";
    const bool wantCredit = action == "credit";

    if (wantPositions) {
        result["positions"] = ExchangePositionsToJson(_server);
    }

    if (!ledger) {
        res.status = 200;
        res.set_content(result.dump(), "application/json");
        return;
    }

    if (wantCapital) {
        result["capital"] = CapitalToJson(*ledger);
    }

    if (wantLedger || wantCredit) {
        nlohmann::json entries = nlohmann::json::object();
        for (const auto& [name, account] : ledger->GetAccounts()) {
            if (!only.empty() && name != only) continue;
            entries[name] = wantCredit ? CreditToJson(account._credit) : LedgerAccountToJson(account);
        }
        result["strategies"] = std::move(entries);
    }

    if (!only.empty() && result.contains("strategies") && result["strategies"].empty()) {
        res.status = 404;
        res.set_content(R"({"error": "strategy not found in ledger"})", "application/json");
        return;
    }

    res.status = 200;
    res.set_content(result.dump(), "application/json");
}
