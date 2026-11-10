#include "Util/DecisionSet.h"
#include "Util/datetime.h"
#include "Bridge/LedgerStore.h"
#include <algorithm>


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
    double accrualDays,
    double projectedCash,
    double projectedEquity,
    bool projectedKnown,
    const Map<symbol_t, Position>& positions,
    bool holdingsKnown)
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
    set._projectedCash = projectedCash;
    set._projectedEquity = projectedEquity;
    set._projectedKnown = projectedKnown;
    set._riskEvents = riskEvents;
    set._costProfile = costProfile;

    // ── 当前持仓（决策前口径）──
    // 持仓来自账本快照，价格取今日收盘价（策略池内的标的）。
    // 池外标的账本里没有今天的收盘价，用账本的标记价（上一个已知价），
    // 与 AgentSubSystem 的盯市口径一致 —— 不给就显示"—"，不编价。
    set._holdingsKnown = holdingsKnown;
    set._borrowedCash = creditState.borrowedCash;
    if (holdingsKnown) {
        for (const auto& [sym, pos] : positions) {
            if (pos._quantity == 0) continue;
            DecisionSet::Holding h;
            h._symbol = sym;
            h._quantity = pos._quantity;
            h._costPrice = pos._avgPrice;
            auto cp = closePrices.find(sym);
            h._currentPrice = (cp != closePrices.end() && cp->second > 0.0) ? cp->second
                                                                          : pos._currentPrice;
            h._marketValue = static_cast<double>(h._quantity) * h._currentPrice;
            // 成本价未知就不算浮盈（读侧显示"—"），不拿 0 冒充"没亏没赚"
            if (h._costPrice > 0.0) {
                h._unrealizedPnl =
                    (h._currentPrice - h._costPrice) * static_cast<double>(h._quantity);
            }
            set._currentPortfolioValue += h._marketValue;
            set._holdings.push_back(std::move(h));
        }
        // 重仓在前：读者先看到仓位最重的标的
        std::sort(set._holdings.begin(), set._holdings.end(),
                  [](const DecisionSet::Holding& a, const DecisionSet::Holding& b) {
                      return a._marketValue > b._marketValue;
                  });
        // 当前总资金 = 持仓市值 + 剩余资金（账本现金，决策前）
        set._currentTotalCapital = set._currentPortfolioValue + set._available;
    }

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
        // 决策后口径：cash 用 projectedCash（决策前 available + 卖出回款 − 买入支出），
        // 否则与 portfolioValue（决策后市值）不是同一时点，分子分母口径错配。
        set._marginEquity = (projectedKnown ? projectedCash : available) + portfolioValue;
        set._usedMargin = creditState.borrowedCash + set._totalShortValue;
    }

    return set;
}
