#include "Nodes/SignalNodeBase.h"
#include "Util/log.h"
#include "Util/FinanceDB.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "ExchangeManager.h"
#include "Bridge/SIM/HistorySimulationBase.h"

bool SignalNodeBase::Init(const nlohmann::json& config) {
    // 是否允许做空
    if (config["params"].contains("allowShort")) {
        _allowShort = config["params"]["allowShort"]["value"];
    }

    // 标的池：BFS 上游拿到（提前到派生类的表达式校验之前，
    // 否则校验阶段拿不到 symbol 上下文）
    Set<symbol_t> upstreamSymbols = discoverUpstreamSymbols();
    if (upstreamSymbols.empty()) {
        WARN("[{}:{}] No upstream symbols found, signal node will be skipped",
             nodeTypeName(), _id);
        return false;
    }
    for (const auto& sym : upstreamSymbols) {
        _pools.emplace_back(sym);
    }
    INFO("[{}:{}] Found {} symbols from upstream", nodeTypeName(), _id, _pools.size());

    // 允许做空时才需要可做空名单
    if (_allowShort) {
        ensureMarginStockData();
    }

    return true;
}

Map<String, ArgType> SignalNodeBase::out_elements() {
    Map<String, ArgType> elems;
    // 信号是整数时间序列
    for (auto& symbol : _pools) {
        elems[get_symbol(symbol) + ".signal"] = ArgType::Integer_TimeSeries;
    }
    return elems;
}

/// 读账户真实持仓（正=多，负=空）
Map<symbol_t, int64_t> SignalNodeBase::loadPositions(const String& strategy) const {
    Map<symbol_t, int64_t> heldSymbols;
    if (_server->GetRunningMode() == RuningType::Backtest) {
        auto* exchangeMgr = _server->GetExchangeManager();
        for (const auto& symbol : _pools) {
            auto* histExchange = dynamic_cast<HistorySimulationBase*>(
                exchangeMgr->ResolveExchange(symbol));
            if (histExchange) {
                auto cnt = histExchange->GetPositionQuantity(symbol);
                if (cnt != 0) {
                    heldSymbols[symbol] = cnt;
                }
            }
        }
    } else {
        auto* broker = _server->GetBrokerSubSystem();
        auto* capital = broker->GetStrategyCapital(strategy);
        for (const auto& [symbol, pos] : capital->getPositions().getAll()) {
            if (pos._quantity != 0) {
                heldSymbols[symbol] = pos._quantity;
            }
        }
    }
    return heldSymbols;
}

/// 做空门控：无持仓的 SELL 降级为 HOLD
void SignalNodeBase::applyShortableFilter(List<Pair<symbol_t, TradeAction>>& actions,
                                          const Map<symbol_t, int64_t>& held) const {
    for (auto& item : actions) {
        if (item.second != TradeAction::SELL) continue;
        if (held.count(item.first)) continue;  // 有持仓 → 平仓，不是开空
        if (!_allowShort) {
            item.second = TradeAction::HOLD;
            continue;
        }
        // 允许做空：只对 margin_stock 名单内的标的保留开空信号
        if (!_shortableSymbols.count(item.first)) {
            item.second = TradeAction::HOLD;
        }
    }
}

/// 已持多仓的标的不再追加买入
void SignalNodeBase::applyNoAddOnPosition(List<Pair<symbol_t, TradeAction>>& actions,
                                          const Map<symbol_t, int64_t>& held) const {
    for (auto& item : actions) {
        if (item.second != TradeAction::BUY) continue;
        auto it = held.find(item.first);
        if (it == held.end()) continue;      // 空仓 → 正常开多
        if (it->second > 0) {
            item.second = TradeAction::HOLD;  // 已有多仓 → 不加仓
        }
        // 已持空仓 → BUY 是买券还券，保留
    }
}

/// 把每个标的的最终动作写进 {symbol}.signal
void SignalNodeBase::writeSignals(DataContext& context,
                                   const Map<symbol_t, TradeAction>& decisions) const {
    // 信号值：1=买入，-1=卖出，0=持有
    for (auto& symbol : _pools) {
        String key = get_symbol(symbol) + ".signal";
        int signalValue = 0;  // 默认持有
        if (decisions.count(symbol)) {
            auto& dec = decisions.at(symbol);
            if (dec == TradeAction::BUY) {
                signalValue = 1;
            } else if (dec == TradeAction::SELL) {
                signalValue = -1;
            }
        }
        // 检查是否已存在，存在则追加，否则创建新向量
        if (context.exist(key)) {
            context.add(key, (double)signalValue);
        } else {
            // 首次写入，预填充 warmup 占位符
            // warmup 期间信号节点被跳过，但 QuoteInputNode 仍然写入了时间
            // 需要预填充 warmup 个 0 值，使数据长度与时间序列对齐
            int warmup = context.GetWarmupEpochs();
            Vector<double> signalVec;
            if (warmup > 0) {
                signalVec.assign(warmup, 0.0);  // warmup 期间信号为 0（持有）
            }
            signalVec.push_back((double)signalValue);
            context.set(key, std::move(signalVec));
        }
    }
}

void SignalNodeBase::ensureMarginStockData() {
    // 只读：margin_stock 是全局数据，由 Server::RefreshMarginStockList 在每周六
    // 的日终时点刷新，策略不负责拉取。此前在这里同步调 fetch_shortable_stocks.py
    // 有三个问题：
    //   1. 全量拉交易所名单（实测 1681 只 / 6 分钟）只为回答本策略几个标的的成员资格；
    //   2. RunCommand 同步阻塞在 Init 里，失败还静默降级，排查时只有一行 WARN；
    //   3. 全局数据的生命周期挂在单策略上，每个策略启动都可能触发一次网络阻塞。
    // 现在读不到就明确告警并禁用做空，刷新动作交给定时任务。
    auto& financeDB = FinanceDB::instance();
    DEBUG_INFO("[{}:{}] margin: FinanceDB initialized={}", nodeTypeName(), _id,
               financeDB.isInitialized());

    if (!financeDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!financeDB.init(db_path + "/finance", "finance.db")) {
            WARN("[{}:{}] 无法初始化 FinanceDB，禁用做空。"
                 "请确认 {} 是否可写", nodeTypeName(), _id, db_path + "/finance/finance.db");
            _allowShort = false;
            return;
        }
    }

    auto result = financeDB.queryMarginStocks();
    size_t total = result.contains("data") && result["data"].is_array()
                   ? result["data"].size() : 0;
    DEBUG_INFO("[{}:{}] margin: {} rows in table", nodeTypeName(), _id, total);

    if (total == 0) {
        WARN("[{}:{}] margin_stock 为空，禁用做空（策略将退化为纯做多）。"
             "名单由 Server::RefreshMarginStockList 每周六日终刷新；"
             "如需立即生效请手工执行 tools/fetch_shortable_stocks.py 并导入 FinanceDB",
             nodeTypeName(), _id);
        _allowShort = false;
        return;
    }

    for (const auto& sym : _pools) {
        String symStr = get_symbol(sym);
        bool isMargin = financeDB.isMarginStock(symStr);
        DEBUG_INFO("[{}:{}] margin: {} isMarginStock={}", nodeTypeName(), _id, symStr, isMargin);
        if (isMargin) {
            _shortableSymbols.insert(sym);
        }
    }

    if (_shortableSymbols.empty()) {
        WARN("[{}:{}] 标的池 {} 只中无一可做空（margin_stock 共 {} 只），"
             "本策略不会有空头仓位", nodeTypeName(), _id, _pools.size(), total);
    } else {
        INFO("[{}:{}] 标的池中可做空 {}/{}（margin_stock 共 {} 只）",
             nodeTypeName(), _id, _shortableSymbols.size(), _pools.size(), total);
    }
}
