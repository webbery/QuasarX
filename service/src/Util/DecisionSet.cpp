#include "Util/DecisionSet.h"
#include "Util/log.h"
#include "Util/datetime.h"

DecisionSet BuildDecisionSet(
    const String& strategy,
    const String& asOf,
    const String& runId,
    const Map<symbol_t, DecisionSnapshot>& decisions,
    const Vector<ProtectionNode::ProtectionEvent>& riskEvents,
    double allocated,
    double available,
    double equity,
    bool budgetKnown,
    double portfolioValue,
    const Map<symbol_t, double>& closePrices,
    DecisionStatus status,
    const String& error)
{
    DecisionSet set;
    set._strategy = strategy;
    set._asOf = asOf;
    set._runId = runId;
    set._generatedAt = ToString(Now(), "%Y-%m-%d %H:%M:%S");
    set._status = status;
    set._error = error;

    set._allocated = allocated;
    set._available = available;
    set._equity = equity;
    set._budgetKnown = budgetKnown;
    set._portfolioValue = portfolioValue;
    set._riskEvents = riskEvents;

    // Map 的 key 就是 symbol，这里直接按 key 取收盘价，
    // 保证 daily_positions 写的是真实收盘价而不是计划目标价。
    set._items.reserve(decisions.size());
    for (const auto& [sym, snap] : decisions) {
        DecisionSet::Item item;
        item._symbol = sym;
        item._action = snap._action;
        item._flag = snap._flag;
        item._quantity = snap._quantity;
        item._price = snap._price;
        item._epoch = snap._epoch;
        item._isDefaultHold = snap._action == TradeAction::HOLD && !snap._signalEvaluated;

        auto it = closePrices.find(sym);
        if (it != closePrices.end()) {
            item._closePrice = it->second;
        }
        set._items.push_back(std::move(item));
    }

    return set;
}

nlohmann::json DecisionSet::toJson() const {
    nlohmann::json j;
    j["strategy"] = _strategy;
    j["as_of"] = _asOf;
    j["run_id"] = _runId;
    j["generated_at"] = _generatedAt;
    j["status"] = to_string(_status);
    if (!_error.empty()) j["error"] = _error;

    j["allocated"] = _allocated;
    j["available"] = _available;
    j["equity"] = _equity;
    j["budget_known"] = _budgetKnown;
    j["portfolio_value"] = _portfolioValue;

    nlohmann::json items = nlohmann::json::array();
    for (const auto& it : _items) {
        nlohmann::json o;
        o["symbol"] = get_symbol(it._symbol);
        o["action"] = static_cast<int>(it._action);   // TradeAction 枚举值
        o["flag"] = static_cast<int>(it._flag);
        o["quantity"] = it._quantity;
        o["price"] = it._price;
        o["close_price"] = it._closePrice;
        o["epoch"] = it._epoch;
        o["is_default_hold"] = it._isDefaultHold;
        items.push_back(std::move(o));
    }
    j["items"] = std::move(items);

    nlohmann::json risks = nlohmann::json::array();
    for (const auto& e : _riskEvents) {
        nlohmann::json o;
        o["bar_index"] = e._bar_index;
        o["datetime"] = static_cast<int64_t>(e._datetime);
        o["symbol"] = get_symbol(e._symbol);
        o["type"] = static_cast<int>(e._type);
        o["entry_price"] = e._entry_price;
        o["current_price"] = e._current_price;
        o["quantity"] = e._quantity;
        risks.push_back(std::move(o));
    }
    j["risk_events"] = std::move(risks);

    return j;
}

DecisionSet DecisionSet::fromJson(const nlohmann::json& j) {
    DecisionSet set;
    if (!j.is_object()) return set;

    set._strategy = j.value("strategy", "");
    set._asOf = j.value("as_of", "");
    set._runId = j.value("run_id", "");
    set._generatedAt = j.value("generated_at", "");
    set._status = parse_decision_status(j.value("status", String()));
    set._error = j.value("error", "");

    set._allocated = j.value("allocated", 0.0);
    set._available = j.value("available", 0.0);
    set._equity = j.value("equity", 0.0);
    set._budgetKnown = j.value("budget_known", false);
    set._portfolioValue = j.value("portfolio_value", 0.0);

    if (j.contains("items") && j["items"].is_array()) {
        set._items.reserve(j["items"].size());
        for (const auto& o : j["items"]) {
            DecisionSet::Item it;
            it._symbol = to_symbol(o.value("symbol", String()));
            it._action = static_cast<TradeAction>(o.value("action", 0));
            it._flag = static_cast<unsigned char>(o.value("flag", 0));
            it._quantity = o.value("quantity", static_cast<int64_t>(0));
            it._price = o.value("price", 0.0);
            it._closePrice = o.value("close_price", 0.0);
            it._epoch = o.value("epoch", -1);
            it._isDefaultHold = o.value("is_default_hold", false);
            set._items.push_back(std::move(it));
        }
    }

    if (j.contains("risk_events") && j["risk_events"].is_array()) {
        set._riskEvents.reserve(j["risk_events"].size());
        for (const auto& o : j["risk_events"]) {
            ProtectionNode::ProtectionEvent e;
            e._bar_index = o.value("bar_index", 0);
            e._datetime = static_cast<time_t>(o.value("datetime", static_cast<int64_t>(0)));
            e._symbol = to_symbol(o.value("symbol", String()));
            e._type = static_cast<RiskTriggerType>(o.value("type", 0));
            e._entry_price = o.value("entry_price", 0.0);
            e._current_price = o.value("current_price", 0.0);
            e._quantity = o.value("quantity", static_cast<int64_t>(0));
            set._riskEvents.push_back(std::move(e));
        }
    }

    return set;
}