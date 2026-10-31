#include "Util/DecisionSet.h"
#include "Util/log.h"
#include "Util/datetime.h"
#include "Bridge/LedgerStore.h"

namespace {
// CostQuote ↔ JSON：map 的 key 是枚举，不能直接 dump，按费用种类名序列化
nlohmann::json CostQuoteToJson(const CostQuote& quote) {
    nlohmann::json trade = nlohmann::json::object();
    for (const auto& [kind, v] : quote._tradeItems) {
        trade[to_string(kind)] = v;
    }
    nlohmann::json holding = nlohmann::json::object();
    for (const auto& [kind, v] : quote._holdingItems) {
        holding[to_string(kind)] = v;
    }
    nlohmann::json j;
    j["trade"] = std::move(trade);
    j["holding"] = std::move(holding);
    j["trade_total"] = quote.tradeTotal();
    j["holding_total"] = quote.holdingTotal();
    return j;
}

void CostQuoteFromJson(const nlohmann::json& j, CostQuote& quote) {
    if (!j.is_object()) return;
    if (j.contains("trade") && j["trade"].is_object()) {
        for (const auto& [key, v] : j["trade"].items()) {
            quote._tradeItems[cost_kind_from_string(key)] = v.get<double>();
        }
    }
    if (j.contains("holding") && j["holding"].is_object()) {
        for (const auto& [key, v] : j["holding"].items()) {
            quote._holdingItems[cost_kind_from_string(key)] = v.get<double>();
        }
    }
}

// 费率档案 → JSON：只序列化交易时点的费率项，够复现口径即可
nlohmann::json CostProfileToJson(const CostProfile& profile) {
    nlohmann::json fees = nlohmann::json::array();
    for (const auto& f : profile._tradeFees) {
        nlohmann::json o;
        o["kind"] = to_string(f._kind);
        o["rate"] = f._rate;
        o["min"] = f._min;
        o["sell_only"] = f._sellOnly;
        fees.push_back(std::move(o));
    }
    nlohmann::json j;
    j["known"] = profile._known;
    j["margin_ratio"] = profile._marginRatio;
    j["trade_fees"] = std::move(fees);
    return j;
}
}  // namespace

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
    const Map<symbol_t, double>& costPrices,
    const CostProfile& costProfile,
    DecisionStatus status,
    const String& error,
    const StrategyCreditState& creditState,
    double marginRatio,
    double maintenanceMarginRatio,
    double warningRatio,
    double liquidationRatio,
    double accrualDays)
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
    set._costProfile = costProfile;

    // 两融配置
    set._marginRatio = marginRatio;
    set._maintenanceMarginRatio = maintenanceMarginRatio;
    set._warningRatio = warningRatio;
    set._liquidationRatio = liquidationRatio;
    set._accrualDays = accrualDays;
    set._financingRate = costProfile.financingRate();
    set._securitiesLendingRate = costProfile.securitiesLendingRate();

    // 检查是否有两融敞口
    set._hasMargin = (creditState.borrowedCash > 0.0 || !creditState.borrowedShares.empty());
    set._marginKnown = (marginRatio > 0.0 && marginRatio <= 1.0);

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
        item._isEmergencyClose = snap._isEmergencyClose;

        auto it = closePrices.find(sym);
        if (it != closePrices.end()) {
            item._closePrice = it->second;
        }

        // 持仓均价（平仓成本价）：优先用决策自带的（紧急平仓在执行计划里就带着成本价），
        // 其次回退到账本均价；都拿不到就是缺失，记 0 让读侧降级显示"—"
        item._costPrice = snap._costPrice;
        if (item._costPrice <= 0.0) {
            auto ct = costPrices.find(sym);
            if (ct != costPrices.end() && ct->second > 0.0) {
                item._costPrice = ct->second;
            }
        }

        // 两融标记
        item._marginRatio = marginRatio;
        if (snap._action == TradeAction::SELL && snap._flag == 0) {
            // 融券卖出
            item._isShortOpen = true;
        } else if (snap._action == TradeAction::BUY && snap._flag == 1) {
            // 买入平空
            item._isShortCover = true;
        } else if (snap._action == TradeAction::BUY && snap._flag == 0 && marginRatio < 1.0) {
            // 融资买入
            item._isFinanced = true;
        }

        // 摩擦成本：按费率档案对每笔交易求值一次，邮件/日报只读结果
        if (item._action != TradeAction::HOLD) {
            item._cost = EvaluateTradeCost(costProfile, DecisionSet::MakeCostContext(item));
            set._totalTradeFriction += item._cost.tradeTotal();
        }
        set._items.push_back(std::move(item));
    }

    // 两融明细与日息求值
    if (set._hasMargin && set._marginKnown) {
        // 构建两融明细
        for (const auto& [symStr, shares] : creditState.borrowedShares) {
            if (shares <= 0) continue;

            DecisionSet::MarginItem margin;
            margin._symbol = to_symbol(symStr);
            margin._borrowedShares = shares;

            // 开空均价
            auto avgIt = creditState.shortAvgPrice.find(symStr);
            if (avgIt != creditState.shortAvgPrice.end()) {
                margin._shortCostPrice = avgIt->second;
            }

            // 融券在借市值（按当期收盘价）
            auto priceIt = closePrices.find(margin._symbol);
            double currentPrice = (priceIt != closePrices.end()) ? priceIt->second : margin._shortCostPrice;
            margin._borrowedValue = static_cast<double>(shares) * currentPrice;

            // 融券费（按日计提）
            HoldingCostContext holdCtx;
            holdCtx._borrowedValue = margin._borrowedValue;
            holdCtx._days = accrualDays;
            CostQuote quote = EvaluateHoldingCost(costProfile, holdCtx);
            margin._dailyShortFee = quote.item(CostKind::SecuritiesLending);

            set._marginItems.push_back(std::move(margin));
            set._totalShortValue += margin._borrowedValue;
            set._totalShortFee += margin._dailyShortFee;
        }

        // 融资明细（按标的）
        for (const auto& [symStr, amount] : creditState.financedAmount) {
            if (amount <= 0.0) continue;

            // 找到对应的 margin item 或新建
            DecisionSet::MarginItem* margin = nullptr;
            for (auto& m : set._marginItems) {
                if (get_symbol(m._symbol) == symStr) {
                    margin = &m;
                    break;
                }
            }
            if (!margin) {
                set._marginItems.emplace_back();
                margin = &set._marginItems.back();
                margin->_symbol = to_symbol(symStr);
            }

            margin->_financedAmount = amount;

            // 融资利息（按日计提）
            HoldingCostContext holdCtx;
            holdCtx._borrowedCash = amount;
            holdCtx._days = accrualDays;
            CostQuote quote = EvaluateHoldingCost(costProfile, holdCtx);
            margin->_dailyFinancingInterest = quote.item(CostKind::Financing);

            set._totalFinancingInterest += margin->_dailyFinancingInterest;
        }

        set._totalMarginCost = set._totalShortFee + set._totalFinancingInterest;
        set._totalHoldingCost = set._totalMarginCost;

        // 口径 B：保证金总量 = 自有资金 + 证券市值（多头持仓）
        // 已占用 = 融资欠款 + 融券市值
        set._marginEquity = available + portfolioValue;
        set._usedMargin = creditState.borrowedCash + set._totalShortValue;
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

    // 交易成本：费率档案 + 当日合计（邮件的摩擦列就是从 items[].cost 读的）
    j["cost_profile"] = CostProfileToJson(_costProfile);
    j["total_trade_friction"] = _totalTradeFriction;
    j["total_holding_cost"] = _totalHoldingCost;

    // 两融（信用交易）
    if (_hasMargin) {
        nlohmann::json marginJson;
        marginJson["has_margin"] = _hasMargin;
        marginJson["margin_known"] = _marginKnown;
        marginJson["margin_ratio"] = _marginRatio;
        marginJson["maintenance_margin_ratio"] = _maintenanceMarginRatio;
        marginJson["financing_rate"] = _financingRate;
        marginJson["securities_lending_rate"] = _securitiesLendingRate;
        marginJson["warning_ratio"] = _warningRatio;
        marginJson["liquidation_ratio"] = _liquidationRatio;
        marginJson["accrual_days"] = _accrualDays;
        marginJson["margin_equity"] = _marginEquity;
        marginJson["used_margin"] = _usedMargin;
        marginJson["total_short_value"] = _totalShortValue;
        marginJson["total_short_fee"] = _totalShortFee;
        marginJson["total_financing_interest"] = _totalFinancingInterest;
        marginJson["total_margin_cost"] = _totalMarginCost;

        nlohmann::json itemsJson = nlohmann::json::array();
        for (const auto& m : _marginItems) {
            nlohmann::json mi;
            mi["symbol"] = get_symbol(m._symbol);
            mi["borrowed_shares"] = m._borrowedShares;
            mi["short_cost_price"] = m._shortCostPrice;
            mi["borrowed_value"] = m._borrowedValue;
            mi["daily_short_fee"] = m._dailyShortFee;
            mi["financed_amount"] = m._financedAmount;
            mi["daily_financing_interest"] = m._dailyFinancingInterest;
            itemsJson.push_back(std::move(mi));
        }
        marginJson["items"] = std::move(itemsJson);
        j["margin"] = std::move(marginJson);
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto& it : _items) {
        nlohmann::json o;
        o["symbol"] = get_symbol(it._symbol);
        o["action"] = static_cast<int>(it._action);   // TradeAction 枚举值
        o["flag"] = static_cast<int>(it._flag);
        o["quantity"] = it._quantity;
        o["price"] = it._price;
        o["close_price"] = it._closePrice;
        o["cost_price"] = it._costPrice;
        o["epoch"] = it._epoch;
        o["is_default_hold"] = it._isDefaultHold;
        o["is_emergency_close"] = it._isEmergencyClose;
        o["is_short_open"] = it._isShortOpen;
        o["is_short_cover"] = it._isShortCover;
        o["is_financed"] = it._isFinanced;
        o["margin_ratio"] = it._marginRatio;
        o["cost"] = CostQuoteToJson(it._cost);
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

    nlohmann::json snaps = nlohmann::json::array();
    for (const auto& s : _positionSnapshots) {
        nlohmann::json o;
        o["symbol"] = get_symbol(s._symbol);
        o["date"] = static_cast<int64_t>(s._date);
        o["position"] = s._position;
        o["close_price"] = s._closePrice;
        snaps.push_back(std::move(o));
    }
    j["position_snapshots"] = std::move(snaps);

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
    set._totalTradeFriction = j.value("total_trade_friction", 0.0);
    set._totalHoldingCost = j.value("total_holding_cost", 0.0);

    // 两融（信用交易）
    if (j.contains("margin") && j["margin"].is_object()) {
        const auto& margin = j["margin"];
        set._hasMargin = margin.value("has_margin", false);
        set._marginKnown = margin.value("margin_known", false);
        set._marginRatio = margin.value("margin_ratio", 1.0);
        set._maintenanceMarginRatio = margin.value("maintenance_margin_ratio", 0.3);
        set._financingRate = margin.value("financing_rate", 0.0);
        set._securitiesLendingRate = margin.value("securities_lending_rate", 0.0);
        set._warningRatio = margin.value("warning_ratio", 1.5);
        set._liquidationRatio = margin.value("liquidation_ratio", 1.3);
        set._accrualDays = margin.value("accrual_days", 1.0);
        set._marginEquity = margin.value("margin_equity", 0.0);
        set._usedMargin = margin.value("used_margin", 0.0);
        set._totalShortValue = margin.value("total_short_value", 0.0);
        set._totalShortFee = margin.value("total_short_fee", 0.0);
        set._totalFinancingInterest = margin.value("total_financing_interest", 0.0);
        set._totalMarginCost = margin.value("total_margin_cost", 0.0);

        if (margin.contains("items") && margin["items"].is_array()) {
            set._marginItems.reserve(margin["items"].size());
            for (const auto& mi : margin["items"]) {
                DecisionSet::MarginItem m;
                m._symbol = to_symbol(mi.value("symbol", String()));
                m._borrowedShares = mi.value("borrowed_shares", static_cast<int64_t>(0));
                m._shortCostPrice = mi.value("short_cost_price", 0.0);
                m._borrowedValue = mi.value("borrowed_value", 0.0);
                m._dailyShortFee = mi.value("daily_short_fee", 0.0);
                m._financedAmount = mi.value("financed_amount", 0.0);
                m._dailyFinancingInterest = mi.value("daily_financing_interest", 0.0);
                set._marginItems.push_back(std::move(m));
            }
        }
    }

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
            it._costPrice = o.value("cost_price", 0.0);
            it._epoch = o.value("epoch", -1);
            it._isDefaultHold = o.value("is_default_hold", false);
            it._isEmergencyClose = o.value("is_emergency_close", false);
            it._isShortOpen = o.value("is_short_open", false);
            it._isShortCover = o.value("is_short_cover", false);
            it._isFinanced = o.value("is_financed", false);
            it._marginRatio = o.value("margin_ratio", 1.0);
            if (o.contains("cost")) {
                CostQuoteFromJson(o["cost"], it._cost);
            }
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

    if (j.contains("position_snapshots") && j["position_snapshots"].is_array()) {
        set._positionSnapshots.reserve(j["position_snapshots"].size());
        for (const auto& o : j["position_snapshots"]) {
            DecisionSet::PositionSnapshot s;
            s._symbol = to_symbol(o.value("symbol", String()));
            s._date = static_cast<time_t>(o.value("date", static_cast<int64_t>(0)));
            s._position = o.value("position", static_cast<int64_t>(0));
            s._closePrice = o.value("close_price", 0.0);
            set._positionSnapshots.push_back(s);
        }
    }

    return set;
}