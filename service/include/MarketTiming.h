#pragma once
#include "Bridge/exchange.h"
#include "Util/DecisionSet.h"

class TradeSignal;
class DataContext;
class Server;
class ITimingStrategy {
public:
    ITimingStrategy(Server* server): _server(server) {}
    virtual ~ITimingStrategy(){}

    virtual bool processSignal(const String& strategy, const TradeSignal& signal, const DataContext& context) = 0;

    const List<Pair<symbol_t, TradeReport>>& GetReports() const {
        return _reports;
    }
protected:
    Server* _server;
    List<Pair<symbol_t, TradeReport>> _reports;
};

/**
 * @brief SSE 载荷条目
 *
 * 动作码由 DecisionSet::Item 派生（decisionAction()），不再手写字符串，
 * 避免 SSE / DuckDB / 日报三处各写一份而互相漂移。
 * decisionId 来自 BrokerSubSystem::AddDecision 的返回值，未入库时传 0。
 */
inline nlohmann::json decisionSseEntry(const DecisionSet::Item& d, int decisionId) {
    DecisionAction da = DecisionAction::OpenLong;
    d.decisionAction(da);

    nlohmann::json o;
    o["id"] = decisionId;
    o["symbol"] = get_symbol(d._symbol);
    o["action"] = decision_action_name(da);
    o["label"] = decision_action_label(da);
    o["quantity"] = d._quantity;
    o["price"] = d._price;
    o["epoch"] = d._epoch;
    return o;
}

/**
 * @brief 决策集合的 SSE 条目数组
 *
 * 只含 BUY/SELL：HOLD 不是一条待执行指令，不进 SSE（app 端订单台也只认这两类）。
 * decisionIds 由入库方回填，未入库的标的 id 为 0。
 */
inline nlohmann::json buildSseDecisions(const DecisionSet& set,
                                        const Map<symbol_t, int>& decisionIds) {
    nlohmann::json arr = nlohmann::json::array();
    auto add = [&](const DecisionSet::Item& d) {
        int id = 0;
        auto it = decisionIds.find(d._symbol);
        if (it != decisionIds.end()) id = it->second;
        arr.push_back(decisionSseEntry(d, id));
    };
    for (const auto* d : set.ofAction(TradeAction::BUY))  add(*d);
    for (const auto* d : set.ofAction(TradeAction::SELL)) add(*d);
    return arr;
}
