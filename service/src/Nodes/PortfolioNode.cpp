#include "Nodes/PortfolioNode.h"
#include "DataContext.h"
#include "ExchangeManager.h"
#include "StrategyNode.h"
#include "Util/system.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "Bridge/exchange.h"
#include "Util/log.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Nodes/QuoteNode.h"
#include "Nodes/Portfolio/PortfolioModel.h"
#include "Algorithms/MVO/MvoTypes.h"
#include <cmath>

namespace {

// ── 丢单告警限频 ──
// generatePlan 的每个丢弃分支每根 bar 都可能命中（例如 300 只标的的预算都不足一手），
// 无条件 WARN 会刷屏；但原先用 DEBUG_INFO 等于不存在 —— Release 下它展开成空，
// 一次开仓被无声吃掉后没有任何痕迹。这里用线程局部计数器：每次运行只打前 N 条明细。
thread_local int t_dropWarned = 0;
constexpr int kDropWarnLimit = 20;

/// 读 context 里某个标量序列的最新值；不存在 / 非有限（浮点）返回 false
template <typename T>
bool ReadLastScalar(DataContext& context, const String& key, T& out) {
    if (!context.exist(key)) return false;
    const auto& var = context.get(key);
    const auto* vec = std::get_if<Vector<T>>(&var);
    if (vec == nullptr || vec->empty()) return false;
    out = vec->back();
    return std::isfinite(static_cast<double>(out));
}

/// 本次丢单是否还要打印明细（超过上限后折叠，只提示一次）
inline bool ShouldWarnDrop(const String& strategy, uint32_t nodeId) {
    ++t_dropWarned;
    if (t_dropWarned <= kDropWarnLimit) {
        return true;
    }
    if (t_dropWarned == kDropWarnLimit + 1) {
        WARN("[PortfolioNode:{}@{}] 丢单告警已达 {} 条上限，后续丢单不再逐条打印",
             strategy, nodeId, kDropWarnLimit);
    }
    return false;
}

}  // namespace

PortfolioNode::PortfolioNode(Server* server)
    : _server(server)
    , _positionRatio(0.5)
{
}

PortfolioNode::~PortfolioNode() = default;

bool PortfolioNode::Init(const nlohmann::json& config) {
    const auto& params = config.contains("params") ? config["params"] : nlohmann::json::object();

    // 仓位比例
    if (params.contains("positionRatio")) {
        _positionRatio = params["positionRatio"]["value"];
    }

    // 是否允许做空
    if (params.contains("allowShort")) {
        _allowShort = params["allowShort"]["value"];
    }

    // 交易池 - 沿路径回溯获取上游标的池。
    // 快速回测模式下特征子图被 CacheFeatureNode 替代，QuoteInputNode 与交易链路断开，
    // 因此必须走基类的 discoverUpstreamSymbols()（同时识别 QuoteInputNode 和 CacheFeatureNode），
    // 与 SignalNode / FunctionNode / EMDNode / CUSUMNode / XGBoostNode 保持一致。
    // 必须先于模型创建：MVO 的因子剥离需要全池做市场代理。
    _pool = discoverUpstreamSymbols();

    if (_pool.empty()) {
        WARN("[PortfolioNode:{}] No symbols found from upstream nodes", _id);
    } else {
        INFO("[PortfolioNode:{}] Found {} symbols from {} upstream nodes",
             _id, _pool.size(), _ins.size());
    }

    // ── 组合计算模型 ──
    // 参数统一用英文键：sizing_method / max_single_pct / max_total_pct /
    // volatility_target / mvo_*。只创建被选中的那一个模型，未选中的方案
    // （以及它们的递推状态）完全不占内存。
    const String method = params.contains("sizing_method")
        ? static_cast<String>(params["sizing_method"]["value"])
        : String("equal");
    _sizing_model = portfolio::createPortfolioModel(portfolio::parseSizingMethod(method));
    if (_sizing_model) {
        _sizing_model->Init(config, _pool);
        INFO("[PortfolioNode:{}] Sizing model = {}", _id, _sizing_model->name());
    }

    return true;
}

void PortfolioNode::Prepare(const String& strategy, DataContext& context) {
    auto* exchangeMgr = _server->GetExchangeManager();
    double initialCapital = exchangeMgr ?
        exchangeMgr->GetTotalAvailableFunds(context.getBacktestRunId()) :
        BACKTEST_INITIAL_CAPITAL;
    context.setInitialCapital(initialCapital);

    // 模型内部状态（卡尔曼滤波器 / EWMA 协方差 / 上期权重）是跨 bar 递推的，
    // 每次运行开始必须重置 —— 否则上一次回测的尾部状态会污染这一次。
    if (_sizing_model) {
        _sizing_model->Prepare();
    }
    _weights.clear();

    // 丢单告警限频计数器按运行复位，保证每次回测都能看到前若干条明细
    t_dropWarned = 0;
}

NodeProcessResult PortfolioNode::Process(const String& strategy, DataContext& context) {
    // 1. 获取上游节点的信号
    Map<symbol_t, TradeAction> decisions;

    for (const auto& symbol : _pool) {
        String signalKey = get_symbol(symbol) + ".signal";
        if (context.exist(signalKey)) {
            const auto& signalVal = context.get<Vector<double>>(signalKey);
            auto signal = signalVal.back();

            if (signal > 0) {
                decisions[symbol] = TradeAction::BUY;
            } else if (signal < 0) {
                decisions[symbol] = TradeAction::SELL;
            } else {
                decisions[symbol] = TradeAction::HOLD;
            }
        }
    }

    // 如果没有信号，保持现有仓位
    if (decisions.empty()) {
        for (const auto& symbol : _pool) {
            decisions[symbol] = TradeAction::HOLD;
        }
    }

    // ── 新增：风控短路检查 ──
    applyRiskContext(context, decisions);

    const bool isBacktest = (_server->GetRunningMode() == RuningType::Backtest);
    // 2. 获取可用资金
    // live/日终：走 StrategyCapital（统一账本，含持仓市值与两融口径）
    // 回测：保持原有 BacktestContext 资金口径，避免改变回测结果
    double targetCapital = 0.0;
    if (!isBacktest) {
        auto* broker = _server->GetBrokerSubSystem();
        auto* strategyCapital = broker->GetStrategyCapital(strategy);

        // 先把本 bar 的最新价格灌进资金账本，否则权益口径的持仓市值为 0
        // （StrategyCapital 只在日终开始时同步数量，不持有行情）。
        for (const auto& symbol : _pool) {
            const QuoteInfo* q = context.GetQuote(symbol);
            if (q && q->_close > 0) {
                strategyCapital->updatePrice(symbol, q->_close);
            } else if (q && q->_open > 0) {
                strategyCapital->updatePrice(symbol, q->_open);
            }
        }

        // SpendableBudget 已按 positionRatio 折算，不能再乘一次 _positionRatio。
        Set<symbol_t> sellSymbols;
        for (const auto& [symbol, action] : decisions) {
            if (action == TradeAction::SELL) {
                sellSymbols.insert(symbol);
            }
        }
        targetCapital = strategyCapital->SpendableBudget(_positionRatio, _allowShort, sellSymbols);
    } else {
        targetCapital = context.getAvailableCapital() * _positionRatio;
    }

    // 负预算 = 超额占用，今天没有可买入的资金。
    // DataContext::getAvailableCapital() 现在会把负余额原样返回（不再被兜底值吞掉），
    // 这里收敛成 0， 0 预算本来就会让所有买单
    // 走 quantity < 100 分支被丢弃 —— 行为一致，但信号清楚得多。
    if (targetCapital < 0.0) {
        if (!isBacktest) {
            STRATEGY_WARN(strategy, "Budget is negative ({}), over-committed; "
                          "treating today's buy budget as 0", targetCapital);
        } else {
            WARN("[PortfolioNode] Budget is negative ({}), over-committed; "
                 "treating today's buy budget as 0", targetCapital);
        }
        targetCapital = 0.0;
    }

    // ── 价格 / 持仓来源 adapter ──
    // live 与 backtest 各一套，通过 adapter 注入 generatePlan。
    // 必须提到 applySizingWeights 之前 —— MVO 要读当前持仓判定"新买入"，
    // 并用上期权重做换手惩罚。

    HistorySimulationBase* histExchange = nullptr;
    BacktestContext* btContext = nullptr;
    if (isBacktest) {
        auto* exchangeMgr = _server->GetExchangeManager();
        for (auto type : context.getExchangeTypes()) {
            histExchange = dynamic_cast<HistorySimulationBase*>(
                exchangeMgr->GetExchangeByType(type));
            if (histExchange) break;
        }
        btContext = histExchange ?
            histExchange->getBacktestContext(context.getBacktestRunId()) : nullptr;
    }

    std::function<double(symbol_t)> getQuotePrice;
    std::function<int64_t(symbol_t)> getPositionQuantity;

    if (isBacktest) {
        getQuotePrice = [histExchange, btContext](symbol_t symbol) -> double {
            if (!histExchange || !btContext) return 0.0;
            uint32_t curIndex = btContext->getCurIndex(symbol);
            if (curIndex > 0) {
                return histExchange->GetPrimitivePrice(symbol, curIndex - 1);
            }
            return 0.0;
        };

        getPositionQuantity = [histExchange](symbol_t symbol) -> int64_t {
            if (!histExchange) return 0;
            return histExchange->GetPositionQuantity(symbol);
        };
    } else {
        Server* server = _server;
        DataContext* ctx = &context;
        getQuotePrice = [server, ctx](symbol_t symbol) -> double {
            const QuoteInfo* q = ctx->GetQuote(symbol);
            if (q && q->_close > 0) return q->_close;
            if (q && q->_open > 0) return q->_open;

            auto* exchangeMgr = server->GetExchangeManager();
            ExchangeInterface* exchange = nullptr;
            for (auto type : ctx->getExchangeTypes()) {
                exchange = exchangeMgr->GetExchangeByType(type);
                if (exchange) break;
            }
            if (!exchange) {
                exchange = exchangeMgr->GetExchangeByType(ExchangeType::EX_STOCK_HIST_SIM);
            }
            if (exchange) {
                QuoteInfo eq = exchange->GetQuote(symbol);
                if (eq._close > 0) return eq._close;
                if (eq._open > 0) return eq._open;
            }
            return 0.0;
        };

        getPositionQuantity = [server, &strategy](symbol_t symbol) -> int64_t {
            auto* broker = server->GetBrokerSubSystem();
            auto* capital = broker->GetStrategyCapital(strategy);
            // 点查：不要为了取一个数量把整张持仓表拷一遍
            return capital->getHoldingQuantity(symbol);
        };
    }

    // ── 组合计算模型调整仓位权重 ──
    applySizingWeights(strategy, context, decisions, getPositionQuantity);

    // 3. 生成执行计划
    ExecutionPlan newPlan = generatePlan(strategy, context, decisions, targetCapital,
                                         getQuotePrice, getPositionQuantity,
                                         isBacktest ? "backtest" : "live");

    // 4. 两腿/多腿原子下单：开仓组必须腿齐，否则整组取消（见函数注释）
    enforceGroupAtomicity(strategy, context, newPlan);

    // 5. 检查是否有变化
    newPlan._hasChanged = isPlanChanged(newPlan);

    // 6. 如果有变化或首次执行，输出执行计划
    if (newPlan._hasChanged || _lastPlan._items.empty()) {
        _lastPlan._items = std::move(newPlan._items);
        _lastPlan._totalCapital = newPlan._totalCapital;
        _lastPlan._usedCapital = newPlan._usedCapital;
        _lastPlan._hasChanged = true;
        _lastPlan._generatedAt = newPlan._generatedAt;
        _lastPlan._source = newPlan._source;

        context.GetExecutionPlan() = _lastPlan;
    } else {
        context.GetExecutionPlan()._hasChanged = false;
    }

    // 7. 保存执行计划摘要到 context（每次 Process 都写入，确保数据完整）
    // 格式: {symbol}.plan_action (1=BUY, -1=SELL, 0=HOLD), {symbol}.plan_qty, {symbol}.plan_price
    // 构建当前 bar 所有 symbol 的 action/qty/price 映射
    std::map<symbol_t, std::tuple<int, int64_t, double>> symData;
    // 先初始化所有 symbol 为 HOLD
    for (const auto& sym : _pool) {
        symData[sym] = {0, 0, 0.0};
    }
    // 填充有计划的 symbol
    for (const auto& item : _lastPlan._items) {
        int actionVal = 0;
        switch (item._action) {
            case TradeAction::BUY:  actionVal = 1; break;
            case TradeAction::SELL: actionVal = -1; break;
            case TradeAction::HOLD: actionVal = 0; break;
            case TradeAction::EXEC:
              break;
            }
        double price = (item._limitPrice > 0) ? item._limitPrice : 0.0;
        symData[item._symbol] = {actionVal, item._quantity, price};
    }
    // 写入 context
    int warmup = context.GetWarmupEpochs();
    auto& times = context.GetTime();
    size_t idx = times.empty() ? 0 : times.size() - 1;
    for (const auto& [sym, data] : symData) {
        String symStr = get_symbol(sym);
        auto [actionVal, qty, price] = data;

        String actionKey = symStr + ".plan_action";
        String qtyKey = symStr + ".plan_qty";
        String priceKey = symStr + ".plan_price";

        if (context.exist(actionKey)) {
            context.add(actionKey, (double)actionVal);
            context.add(qtyKey, (double)qty);
            context.add(priceKey, price);
        } else {
            // 首次写入，预填充 warmup 占位符
            // warmup 期间 PortfolioNode 被跳过，但 QuoteInputNode 仍然写入了时间
            // 需要预填充 warmup 个 0 值，使数据长度与时间序列对齐
            size_t fillCount = (warmup > 0 && idx > 0) ? idx : 0;
            Vector<double> actionVec(fillCount, 0.0);
            Vector<double> qtyVec(fillCount, 0.0);
            Vector<double> priceVec(fillCount, 0.0);
            actionVec.push_back((double)actionVal);
            qtyVec.push_back((double)qty);
            priceVec.push_back(price);
            context.set(actionKey, std::move(actionVec));
            context.set(qtyKey, std::move(qtyVec));
            context.set(priceKey, std::move(priceVec));
        }
    }

    // ── DuckDB node_io 日志（仅实盘模式）──
    NODE_IO_LOG("portfolio", _id,
        input["capital"] = context.getAvailableCapital();
        input["available_funds"] = targetCapital;
        nlohmann::json decisions_json = nlohmann::json::object();
        for (const auto& [sym, action] : decisions) {
            String action_str = "HOLD";
            if (action == TradeAction::BUY) action_str = "BUY";
            else if (action == TradeAction::SELL) action_str = "SELL";
            decisions_json[get_symbol(sym)] = action_str;
        }
        input["decisions"] = decisions_json;

        nlohmann::json plan_arr = nlohmann::json::array();
        for (const auto& item : _lastPlan._items) {
            nlohmann::json item_json;
            item_json["symbol"] = get_symbol(item._symbol);
            switch (item._action) {
                case TradeAction::BUY:  item_json["action"] = "BUY"; break;
                case TradeAction::SELL: item_json["action"] = "SELL"; break;
                case TradeAction::HOLD: item_json["action"] = "HOLD"; break;
                default: item_json["action"] = "EXEC"; break;
            }
            item_json["quantity"] = item._quantity;
            item_json["limit_price"] = item._limitPrice;
            item_json["target_value"] = item._targetValue;
            plan_arr.push_back(item_json);
        }
        nlohmann::json skipped = nlohmann::json::array();
        output["plan_items"] = plan_arr;
        output["skipped"] = skipped;

        meta["sizing_method"] = _sizing_model ? _sizing_model->name() : "equal";
        if (!_weights.empty()) {
            nlohmann::json weights_json = nlohmann::json::object();
            for (const auto& [sym, w] : _weights) {
                weights_json[get_symbol(sym)] = w;
            }
            meta["sizing_weights"] = weights_json;
        }
    );

    return NodeProcessResult::Success;
}

// 合并后的 generatePlan：通过 adapter 注入持仓与价格来源
ExecutionPlan PortfolioNode::generatePlan(
    const String& strategy,
    DataContext& context,
    const Map<symbol_t, TradeAction>& decisions,
    double targetCapital,
    std::function<double(symbol_t)> getQuotePrice,
    std::function<int64_t(symbol_t)> getPositionQuantity,
    const String& source
) {
    ExecutionPlan plan;
    plan._totalCapital = targetCapital;
    plan._usedCapital = 0.0;
    plan._hasChanged = false;
    plan._generatedAt = context.GetEpoch();
    plan._source = source;

    if (decisions.empty()) {
        return plan;
    }

    // 权重表由 sizing 模型保证覆盖全部建仓腿（PortfolioModel::compute 的契约）。
    // 缺权重 = 模型没给这一腿预算 → 不建仓（0）。不再按决策数兜底：
    // 那个分母含 HOLD / 平仓腿 / 已持仓不加仓的 BUY，会把预算摊给不交易的标的，
    // 也会掩盖模型漏腿的 bug。
    auto getSymbolCapital = [&](symbol_t symbol) -> double {
        const auto it = _weights.find(symbol);
        return (it != _weights.end()) ? targetCapital * it->second : 0.0;
    };

    for (const auto& [symbol, action] : decisions) {
        ExecutionItem item;
        item._symbol = symbol;
        item._action = action;

        double price = getQuotePrice(symbol);

        if (price <= 0) {
            if (_server->GetRunningMode() != RuningType::Backtest) {
                STRATEGY_WARN(strategy, "Invalid price for symbol {}", get_symbol(item._symbol));
            } else {
                WARN("Invalid price for symbol {}", get_symbol(item._symbol));
            }
            continue;
        }

        int64_t currentQty = getPositionQuantity(symbol);

        if (item._action == TradeAction::HOLD) {
            if (currentQty > 0) {
                item._quantity = static_cast<int>(currentQty);
                item._limitPrice = price;
                item._targetValue = item._quantity * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            }
        } else if (item._action == TradeAction::BUY) {
            if (currentQty < 0) {
                // 有空仓，BUY = 平空
                item._quantity = static_cast<int>(-currentQty);
                item._flag = 1; // 平仓
                item._limitPrice = price;
                item._targetValue = item._quantity * price;
                plan._items.push_back(item);
            } else if (currentQty > 0) {
                // 已有多仓，跳过
                if (ShouldWarnDrop(strategy, _id)) {
                    WARN("[PortfolioNode:{}@{}] BUY {} 未生成执行项：已持多仓 {} 股，不再加仓",
                         strategy, _id, get_symbol(item._symbol), currentQty);
                }
                continue;
            } else {
                // 无仓，BUY = 开多
                int quantity = static_cast<int>(getSymbolCapital(symbol) / price / 100) * 100;
                if (quantity < 100) {
                    // 预算不足一手 —— 这是"开仓被静默丢弃"最常见的原因：
                    // targetCapital 为 0（现金为负被收敛）时两只腿都会走到这里
                    if (ShouldWarnDrop(strategy, _id)) {
                        WARN("[PortfolioNode:{}@{}] BUY {} 未生成执行项：数量 {} < 100 股"
                             "（预算={:.0f}, 价格={:.2f}）",
                             strategy, _id, get_symbol(item._symbol), quantity,
                             getSymbolCapital(symbol), price);
                    }
                    // 仍然更新 signal 的 quantity/price，让 ExecuteNode 知道此 signal 已被评估
                    auto* signal = context.getSignalBySymbol(symbol);
                    if (signal) {
                        signal->SetQuantity(quantity);
                        signal->SetPrice(price);
                    }
                    continue;
                }
                item._quantity = quantity;
                item._flag = 0; // 开仓
                item._limitPrice = price;
                item._targetValue = quantity * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            }
        } else if (item._action == TradeAction::SELL) {
            if (currentQty > 0) {
                // 平多
                item._quantity = static_cast<int>(currentQty);
                item._flag = 1; // 平仓
                item._limitPrice = price;
                item._targetValue = currentQty * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            } else if (currentQty == 0 && _allowShort) {
                // 做空（开仓）
                int quantity = static_cast<int>(getSymbolCapital(symbol) / price / 100) * 100;
                if (quantity >= 100) {
                    item._quantity = quantity;
                    item._flag = 0; // 开仓
                    item._limitPrice = price;
                    item._targetValue = quantity * price;
                    plan._items.push_back(item);
                    plan._usedCapital += item._targetValue;
                } else {
                    // 预算不足一手时开空会被丢弃 —— 与 BUY 分支同一类丢单
                    if (ShouldWarnDrop(strategy, _id)) {
                        WARN("[PortfolioNode:{}@{}] SELL(开空) {} 未生成执行项：数量 {} < 100 股"
                             "（预算={:.0f}, 价格={:.2f}）",
                             strategy, _id, get_symbol(item._symbol), quantity,
                             getSymbolCapital(symbol), price);
                    }
                    // 与 BUY 分支一致：写回已评估的 quantity/price，
                    // 让 ExecuteNode 知道这条 signal 已被评估过。
                    auto* signal = context.getSignalBySymbol(symbol);
                    if (signal) {
                        signal->SetQuantity(quantity);
                        signal->SetPrice(price);
                    }
                }
            } else {
                // 空仓且不允许做空 → SELL 无处落地，整条腿被丢弃。
                // 配对策略里这会退化成"只有多头腿"的裸多仓位
                if (ShouldWarnDrop(strategy, _id)) {
                    WARN("[PortfolioNode:{}@{}] SELL {} 未生成执行项：无持仓且未开启做空"
                         "（allowShort={}），该腿被丢弃",
                         strategy, _id, get_symbol(item._symbol), _allowShort);
                }
            }
        }
    }

    // 资金闸门：买入总额不应超过账本可用现金 + 本轮卖出回款。
    // 超了说明预算口径与账本脱节（例如权益重复计入了已花掉的现金）。
    // 这里做硬拦：
    //   1. 现金为负（超额占用）→ 丢弃所有买入项（账户已超配，不能再开新仓）
    //   2. 买入总额超过可用现金+卖出回款 → 按比例缩减买入（避免超买）
    if (_server->GetRunningMode() != RuningType::Backtest) {
        auto* broker = _server->GetBrokerSubSystem();
        const double cash = broker ? broker->GetLedger()->GetAvailable(strategy) : 0.0;

        // 统计买入/卖出总额
        double buyNotional = 0.0;
        double sellNotional = 0.0;
        for (const auto& item : plan._items) {
            if (item._action == TradeAction::BUY) buyNotional += item._targetValue;
            else if (item._action == TradeAction::SELL) sellNotional += item._targetValue;
        }

        // 闸门 1：超额占用 → 禁止开新仓
        if (cash < 0.0) {
            if (buyNotional > 0.0) {
                STRATEGY_WARN(strategy,
                              "Cash is negative ({:.0f}), over-committed; dropping all {} BUY items "
                              "(notional {:.0f}). Please verify sell proceeds before opening new positions.",
                              cash, plan._items.size(), buyNotional);
                // 丢弃所有买入项
                Vector<ExecutionItem> filtered;
                for (const auto& item : plan._items) {
                    if (item._action != TradeAction::BUY) {
                        filtered.push_back(item);
                    }
                }
                plan._items = std::move(filtered);
                // 重新计算 usedCapital
                plan._usedCapital = 0.0;
                for (const auto& item : plan._items) {
                    if (item._action == TradeAction::BUY) {
                        plan._usedCapital += item._targetValue;
                    }
                }
            }
        }
        // 闸门 2：超预算 → 按比例缩减
        else if (buyNotional > cash + sellNotional + 1.0) {
            const double budget = cash + sellNotional;
            const double scale = budget / buyNotional;
            STRATEGY_WARN(strategy,
                          "Plan buy notional {:.0f} exceeds budget {:.0f} (cash {:.0f} + sell {:.0f}); "
                          "scaling BUY items by {:.2f}",
                          buyNotional, budget, cash, sellNotional, scale);
            // 按比例缩减每个买入项的数量（向下取整到 100 股）
            for (auto& item : plan._items) {
                if (item._action == TradeAction::BUY && item._limitPrice > 0.0) {
                    const int64_t newQty = static_cast<int64_t>(
                        std::floor(item._quantity * scale / 100.0)) * 100;
                    if (newQty < 100) {
                        // 缩减后不足一手，丢弃该项
                        item._action = TradeAction::HOLD;
                        item._quantity = 0;
                    } else {
                        item._quantity = static_cast<int>(newQty);
                        item._targetValue = newQty * item._limitPrice;
                    }
                }
            }
            // 重新计算 usedCapital
            plan._usedCapital = 0.0;
            for (const auto& item : plan._items) {
                if (item._action == TradeAction::BUY) {
                    plan._usedCapital += item._targetValue;
                }
            }
        }
    }

    return plan;
}

// ── 定仓组原子下单：开仓 all-or-none ──
//
// generatePlan 是逐 symbol 独立生成执行项的：任一腿可能因为数量不足一手、
// 价格无效、不允许融券、实盘资金闸门缩减而被单独丢掉，同组的另一条腿却
// 照常下发 → 账户里留下一条未对冲的裸腿。生产者每根 bar 会发布
// {sym}.group_id（回退旧的 .pair_id，同组同值），这里按分组做最后一道闸门：
//   - 开仓组：腿必须齐（≥2 且全部开仓），否则整组降级 HOLD；
//   - 平仓组：允许单腿 —— 平仓不新增敞口，裸腿清理也只能发一条腿。
// 不再要求"多空方向都有"：方向与组内比例已经由 share 表达，同向多腿组
// （篮子 / 分层加仓）是合法形态。
// 资金闸门（generatePlan 末尾）先跑，它缩减后产生的单腿同样会被这里拦下。
void PortfolioNode::enforceGroupAtomicity(const String& strategy,
                                          DataContext& context,
                                          ExecutionPlan& plan) {
    // group_id -> 计划项下标。没有 group_id 的标的（独立腿）不参与。
    Map<uint32_t, Vector<size_t>> groups;
    for (size_t i = 0; i < plan._items.size(); ++i) {
        const auto& item = plan._items[i];
        if (item._action != TradeAction::BUY && item._action != TradeAction::SELL) {
            continue;
        }
        double groupId = 0.0;
        if (!ReadLastScalar(context, get_symbol(item._symbol) + ".group_id", groupId) &&
            !ReadLastScalar(context, get_symbol(item._symbol) + ".pair_id", groupId)) {
            continue;
        }
        if (groupId <= 0.0) {
            continue;
        }
        groups[static_cast<uint32_t>(groupId)].push_back(i);
    }

    for (auto& [groupId, idxs] : groups) {
        // 纯平仓组直接放行
        bool allClose = true;
        for (auto i : idxs) {
            if (plan._items[i]._flag == 0) {
                allClose = false;
                break;
            }
        }
        if (allClose) continue;

        // 开仓组：≥2 条腿且全部开仓（不假设方向组合）
        bool ok = idxs.size() >= 2;
        for (auto i : idxs) {
            if (plan._items[i]._flag != 0) {
                ok = false;
                break;
            }
        }
        if (ok) continue;

        String detail;
        for (auto i : idxs) {
            const auto& item = plan._items[i];
            if (!detail.empty()) detail += ", ";
            detail += get_symbol(item._symbol);
            detail += (item._action == TradeAction::BUY) ? " BUY" : " SELL";
        }
        if (ShouldWarnDrop(strategy, _id)) {
            WARN("[PortfolioNode:{}@{}] 定仓组开仓腿不全（group_id={}, plan 内腿数={}: {}），"
                 "整组降级为 HOLD —— 单腿开仓会留下未对冲/未配平的裸腿",
                 strategy, _id, groupId, idxs.size(), detail);
        }
        for (auto i : idxs) {
            auto& item = plan._items[i];
            item._action = TradeAction::HOLD;
            item._quantity = 0;
            item._targetValue = 0.0;
        }
    }

    // 重算已用资金（generatePlan 与上面的降级都按 BUY 名义金额统计）
    plan._usedCapital = 0.0;
    for (const auto& item : plan._items) {
        if (item._action == TradeAction::BUY) {
            plan._usedCapital += item._targetValue;
        }
    }
}

bool PortfolioNode::isPlanChanged(const ExecutionPlan& newPlan) {
    if (_lastPlan._items.size() != newPlan._items.size()) {
        return true;
    }

    for (size_t i = 0; i < newPlan._items.size(); ++i) {
        const auto& oldItem = _lastPlan._items[i];
        const auto& newItem = newPlan._items[i];

        if (oldItem._symbol != newItem._symbol ||
            oldItem._action != newItem._action ||
            oldItem._quantity != newItem._quantity ||
            oldItem._flag != newItem._flag ||
            // 价格也要算"变化"：股数按资金/价格取整，两天价格不同也可能算出同量，
            // 只比数量会把限价留在上一根 bar 上（ExecuteNode 只在计划变化时翻译 plan）。
            oldItem._limitPrice != newItem._limitPrice) {
            return true;
        }
    }

    return false;
}

// ── 风控短路：只把触发风控的标的改为 SELL ──
// 原实现把全部 decisions 都改成 SELL，等于"一只标的触发 → 清空整个组合"，
// 其余持仓会被连带平掉并打上"紧急"标记。现在只处理带标的的触发集合；
// 集合为空（风控未定位到具体标的）时才退回全局平仓语义。
void PortfolioNode::applyRiskContext(DataContext& context, Map<symbol_t, TradeAction>& decisions) {
    auto* rc = context.GetRiskContext();
    if (!rc || !rc->Triggered()) {
        return;
    }

    INFO("[PortfolioNode] {} RiskContext triggered (type={}, symbols={})",
            to_string(rc->_trigger_type), context.CurrentStrategy(), rc->_trigger_symbols.size());

    for (auto& [symbol, action] : decisions) {
        if (rc->_trigger_symbols.count(symbol) == 0) continue;
        if (action != TradeAction::SELL) {
            action = TradeAction::SELL;
        }
    }
}

// ── 构造本轮目标建仓腿 ──
//
// 建仓腿 = 真正占用资金的新开仓：
//   BUY  且当前无持仓              → +1 做多
//   SELL 且当前无持仓 且允许做空    → −1 做空
// 平仓腿（有持仓）不占预算，不进这张表。判定口径必须与 generatePlan 的
// 建仓分支严格一致。
//
// 分组：
//   - 生产者发布 {sym}.group_id（回退旧的 .pair_id），同组同值；
//   - 同组的腿连续存放，组内 share = strength / Σstrength（缺失按 1）；
//   - 组必须完整：组内任何一条腿不建仓（持仓 / HOLD / 不允许做空）→ 整组跳过。
//     否则权重层会给"半个组"定价（另一半已持仓），名义直接翻倍。
void PortfolioNode::buildLegs(
        DataContext& context,
        const Map<symbol_t, TradeAction>& decisions,
        const std::function<int64_t(symbol_t)>& getPositionQuantity,
        Vector<portfolio::PortfolioLeg>& legs,
        Vector<String>& group_keys) const {

    const auto dirOf = [&](symbol_t symbol) -> int {
        const auto it = decisions.find(symbol);
        if (it == decisions.end()) {
            return 0;
        }
        if (it->second == TradeAction::BUY) {
            return 1;
        }
        if (it->second == TradeAction::SELL && _allowShort) {
            return -1;
        }
        return 0;
    };
    const auto qtyOf = [&](symbol_t symbol) -> int64_t {
        return getPositionQuantity ? getPositionQuantity(symbol) : 0;
    };
    const auto strengthOf = [&](symbol_t symbol) -> double {
        double strength = 0.0;
        if (!ReadLastScalar(context, get_symbol(symbol) + ".strength", strength) ||
            strength <= 0.0) {
            strength = 1.0;   // 没有 strength 的标的按等权腿处理
        }
        return strength;
    };
    const auto groupOf = [&](symbol_t symbol) -> uint32_t {
        double group_id = 0.0;
        if (!ReadLastScalar(context, get_symbol(symbol) + ".group_id", group_id) &&
            !ReadLastScalar(context, get_symbol(symbol) + ".pair_id", group_id)) {
            return 0;   // 没有分组 → 独立腿
        }
        return (group_id > 0.0) ? static_cast<uint32_t>(group_id) : 0;
    };

    // 1) 按组收集本轮有决策的标的
    Map<uint32_t, Vector<symbol_t>> groups;
    Vector<symbol_t> singles;
    for (const auto& [symbol, action] : decisions) {
        const uint32_t group = groupOf(symbol);
        if (group != 0) {
            groups[group].push_back(symbol);
        } else {
            singles.push_back(symbol);
        }
    }

    // 2) 组：整组都要建仓，组内按 strength 归一化。
    //    组 id 是**稠密索引**（1 起），原始 id（pair_id / group_id）只用来
    //    生成组级统计键 —— 腿结构体因此只需要 16 bit 存组 id。
    group_keys.assign(1, String());   // index 0 占位：独立腿的键 = 标的代码
    for (const auto& [raw_group, symbols] : groups) {
        bool complete = !symbols.empty();
        for (const auto& symbol : symbols) {
            if (dirOf(symbol) == 0 || qtyOf(symbol) != 0) {
                complete = false;
                break;
            }
        }
        if (!complete) {
            continue;
        }

        double strength_sum = 0.0;
        Vector<double> strengths;
        strengths.reserve(symbols.size());
        for (const auto& symbol : symbols) {
            const double s = strengthOf(symbol);
            strengths.push_back(s);
            strength_sum += s;
        }

        group_keys.push_back(portfolio::groupKey(raw_group, symbols.front()));
        const unsigned group = static_cast<unsigned>(group_keys.size() - 1);

        legs.reserve(legs.size() + symbols.size());
        for (size_t i = 0; i < symbols.size(); ++i) {
            portfolio::PortfolioLeg leg;
            leg._symbol = symbols[i];
            leg._dir = dirOf(symbols[i]);
            leg._group = group;
            leg._share = (strength_sum > 0.0)
                ? static_cast<float>(strengths[i] / strength_sum)
                : 1.0f;
            legs.push_back(leg);
        }
    }

    // 3) 独立腿：各自成组（_group = 0），share = 1
    for (const auto& symbol : singles) {
        if (dirOf(symbol) == 0 || qtyOf(symbol) != 0) {
            continue;   // 不建仓 / 平仓腿
        }
        portfolio::PortfolioLeg leg;
        leg._symbol = symbol;
        leg._dir = dirOf(symbol);
        leg._group = 0;
        leg._share = 1.0f;
        legs.push_back(leg);
    }
}

// ── 按组合计算模型调整仓位权重 ──
//
// 模型只回答"每个定仓单元占多少预算"；方向、组内拆分、上限截断都在
// portfolio 层完成。模型输出保证覆盖全部建仓腿（PortfolioModel::compute）。
void PortfolioNode::applySizingWeights(
        const String& strategy,
        DataContext& context,
        const Map<symbol_t, TradeAction>& decisions,
        const std::function<int64_t(symbol_t)>& getPositionQuantity) {

    _weights.clear();
    if (!_sizing_model) {
        return;
    }

    Vector<portfolio::PortfolioLeg> legs;
    Vector<String> group_keys;
    buildLegs(context, decisions, getPositionQuantity, legs, group_keys);
    if (legs.empty()) {
        return;
    }

    const bool isBacktest = (_server->GetRunningMode() == RuningType::Backtest);
    auto warn = [&strategy, isBacktest](const String& msg) {
        if (isBacktest) {
            DEBUG_INFO("[PortfolioNode] {}", msg);
        } else {
            STRATEGY_WARN(strategy, "{}", msg);
        }
    };

    portfolio::PortfolioContext model_ctx{
        context, std::move(legs), std::move(group_keys), warn};
    _weights = _sizing_model->compute(model_ctx);
}


const nlohmann::json PortfolioNode::getParams() {
    return {"positionRatio", "pool", "allowShort", "sizing_method",
            "max_single_pct", "max_total_pct", "max_gross_pct", "max_net_pct",
            "volatility_target", "strength",
            // Kelly
            "kelly_min_samples",
            // MVO
            "mvo_mean_method", "mvo_cov_method",
            "mvo_risk_aversion", "mvo_turnover_penalty", "mvo_turnover_horizon",
            "mvo_cost_rate", "mvo_max_weight",
            "mvo_vol_target", "mvo_vol_target_enabled",
            "mvo_min_samples", "mvo_min_names"};
}

Map<String, ArgType> PortfolioNode::out_elements() {
    Map<String, ArgType> elems;
    // 声明执行计划摘要数据，供 DebugNode 读取
    // 每个 symbol 有: .plan_action (1=BUY, -1=SELL, 0=HOLD), .plan_qty, .plan_price
    for (const auto& sym : _pool) {
        String s = get_symbol(sym);
        elems[s + ".plan_action"] = ArgType::Double_TimeSeries;
        elems[s + ".plan_qty"] = ArgType::Double_TimeSeries;
        elems[s + ".plan_price"] = ArgType::Double_TimeSeries;
    }
    return elems;
}
