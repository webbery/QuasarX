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
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// ── 丢单告警限频 ──
// generatePlan 的每个丢弃分支每根 bar 都可能命中（例如 300 只标的的预算都不足一手），
// 无条件 WARN 会刷屏；但原先用 DEBUG_INFO 等于不存在 —— Release 下它展开成空，
// 一次开仓被无声吃掉后没有任何痕迹。这里用线程局部计数器：每次运行只打前 N 条明细。
thread_local int t_dropWarned = 0;
constexpr int kDropWarnLimit = 20;

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

    // ── 组合计算模型 ──
    // 参数统一用英文键：sizing_method / max_single_pct / max_total_pct /
    // volatility_target / mvo_*。只创建被选中的那一个模型，未选中的方案
    // （以及它们的递推状态）完全不占内存。
    const String method = params.contains("sizing_method")
        ? static_cast<String>(params["sizing_method"]["value"])
        : String("equal");
    _sizing_model = portfolio::createPortfolioModel(portfolio::parseSizingMethod(method));
    if (_sizing_model) {
        _sizing_model->Init(config);
        INFO("[PortfolioNode:{}] Sizing model = {}", _id, _sizing_model->name());
    }

    // 交易池 - 沿路径回溯获取上游标的池。
    // 快速回测模式下特征子图被 CacheFeatureNode 替代，QuoteInputNode 与交易链路断开，
    // 因此必须走基类的 discoverUpstreamSymbols()（同时识别 QuoteInputNode 和 CacheFeatureNode），
    // 与 SignalNode / FunctionNode / EMDNode / CUSUMNode / XGBoostNode 保持一致。
    _pool = discoverUpstreamSymbols();

    if (_pool.empty()) {
        WARN("[PortfolioNode:{}] No symbols found from upstream nodes", _id);
    } else {
        INFO("[PortfolioNode:{}] Found {} symbols from {} upstream nodes",
             _id, _pool.size(), _ins.size());
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

    // 2. 获取可用资金
    // live/日终：走 StrategyCapital（统一账本，含持仓市值与两融口径）
    // 回测：保持原有 BacktestContext 资金口径，避免改变回测结果
    double targetCapital = 0.0;
    if (_server->GetRunningMode() != RuningType::Backtest) {
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
    // 这里收敛成 0：负数继续流进 getSymbolCapital()/generatePlan() 只会得到
    // "Quantity -500 too small" 这种读不懂的日志，而 0 预算本来就会让所有买单
    // 走 quantity < 100 分支被丢弃 —— 行为一致，但信号清楚得多。
    if (targetCapital < 0.0) {
        if (_server->GetRunningMode() != RuningType::Backtest) {
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
    const bool isBacktest = (_server->GetRunningMode() == RuningType::Backtest);

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
    applySizingWeights(strategy, context, decisions, targetCapital,
                       getQuotePrice, getPositionQuantity);

    // 3. 生成执行计划
    ExecutionPlan newPlan = generatePlan(strategy, context, decisions, targetCapital,
                                         getQuotePrice, getPositionQuantity,
                                         isBacktest ? "backtest" : "live");

    // 4. 检查是否有变化
    newPlan._hasChanged = isPlanChanged(newPlan);

    // 5. 如果有变化或首次执行，输出执行计划
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

    // 6. 保存执行计划摘要到 context（每次 Process 都写入，确保数据完整）
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

    size_t count = decisions.size();
    double perSymbolCapital = targetCapital / count;

    // Strength 模式：按归一化权重分配资金
    auto getSymbolCapital = [&](symbol_t symbol) -> double {
        if (!_weights.empty() && _weights.count(symbol)) {
            return targetCapital * _weights.at(symbol);
        }
        return perSymbolCapital;
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

// ── 按组合计算模型调整仓位权重 ──
void PortfolioNode::applySizingWeights(
        const String& strategy,
        DataContext& context,
        Map<symbol_t, TradeAction>& decisions,
        double targetCapital,
        const std::function<double(symbol_t)>& getQuotePrice,
        const std::function<int64_t(symbol_t)>& getPositionQuantity) {

    _weights.clear();
    if (!_sizing_model) {
        return;
    }

    portfolio::PortfolioContext model_ctx;
    model_ctx._data = &context;
    model_ctx._symbol_pool = &_pool;
    model_ctx._target_capital = targetCapital;
    model_ctx._get_quote_price = getQuotePrice;
    model_ctx._get_position_qty = getPositionQuantity;
    model_ctx._buy_symbols.reserve(decisions.size());
    for (const auto& [symbol, action] : decisions) {
        if (action == TradeAction::BUY) {
            model_ctx._buy_symbols.push_back(symbol);
        }
    }

    const bool isBacktest = (_server->GetRunningMode() == RuningType::Backtest);
    model_ctx._warn = [&strategy, isBacktest](const String& msg) {
        if (isBacktest) {
            DEBUG_INFO("[PortfolioNode] {}", msg);
        } else {
            STRATEGY_WARN(strategy, "{}", msg);
        }
    };

    _weights = _sizing_model->compute(model_ctx);
}


const nlohmann::json PortfolioNode::getParams() {
    return {"positionRatio", "pool", "allowShort", "sizing_method",
            "max_single_pct", "max_total_pct", "volatility_target", "strength",
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
