#include "Nodes/PortfolioNode.h"
#include "DataContext.h"
#include "ExchangeManager.h"
#include "StrategyNode.h"
#include "Util/system.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "Bridge/exchange.h"
#include "Util/log.h"
#include "PortfolioSubsystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Nodes/QuoteNode.h"
#include <cmath>

PortfolioNode::PortfolioNode(Server* server)
    : _server(server)
    , _positionRatio(0.5)
{
}

PortfolioNode::~PortfolioNode() = default;

bool PortfolioNode::Init(const nlohmann::json& config) {
    // 仓位比例
    if (config["params"].contains("positionRatio")) {
        _positionRatio = config["params"]["positionRatio"]["value"];
    }

    // 是否允许做空
    if (config["params"].contains("allowShort")) {
        _allowShort = config["params"]["allowShort"]["value"];
    }

    // ── 新增：仓位 sizing 方法（默认 Equal，保持原有行为） ──
    // 支持中文键（前端格式）和英文键（toServerKey 转换后 / 嵌套格式）
    if (config["params"].contains("仓位计算方法") || config["params"].contains("sizing_method")) {
        String method;
        if (config["params"].contains("仓位计算方法")) {
            method = (String)config["params"]["仓位计算方法"]["value"];
        } else {
            method = (String)config["params"]["sizing_method"]["value"];
        }
        if (method == "kelly") {
            _sizing_method = SizingMethod::Kelly;
        } else if (method == "volatility_target") {
            _sizing_method = SizingMethod::VolatilityTarget;
        } else if (method == "strength") {
            _sizing_method = SizingMethod::Strength;
        } else {
            _sizing_method = SizingMethod::Equal;
        }
    }
    if (config["params"].contains("单标的上限") || config["params"].contains("max_single_pct")) {
        if (config["params"].contains("单标的上限")) {
            _max_single_pct = config["params"]["单标的上限"]["value"];
        } else {
            _max_single_pct = config["params"]["max_single_pct"]["value"];
        }
    }
    if (config["params"].contains("总仓位上限") || config["params"].contains("max_total_pct")) {
        if (config["params"].contains("总仓位上限")) {
            _max_total_pct = config["params"]["总仓位上限"]["value"];
        } else {
            _max_total_pct = config["params"]["max_total_pct"]["value"];
        }
    }
    if (config["params"].contains("波动率目标") || config["params"].contains("volatility_target")) {
        if (config["params"].contains("波动率目标")) {
            _vol_target = config["params"]["波动率目标"]["value"];
        } else {
            _vol_target = config["params"]["volatility_target"]["value"];
        }
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

    // ── 新增：仓位 sizing 调整 ──
    applySizingWeights(context, decisions, targetCapital);

    // 3. 生成执行计划（合并 live/backtest，通过 adapter 注入持仓与价格来源）
    ExecutionPlan newPlan;
    if (_server->GetRunningMode() != RuningType::Backtest) {
        // Live adapter
        auto getQuotePrice = [&](symbol_t symbol) -> double {
            const QuoteInfo* q = context.GetQuote(symbol);
            if (q && q->_close > 0) return q->_close;
            if (q && q->_open > 0) return q->_open;

            auto* exchangeMgr = _server->GetExchangeManager();
            ExchangeInterface* exchange = nullptr;
            for (auto type : context.getExchangeTypes()) {
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

        auto getPositionQuantity = [&](symbol_t symbol) -> int64_t {
            auto* broker = _server->GetBrokerSubSystem();
            auto* capital = broker->GetStrategyCapital(strategy);
            return capital->getPositions().getHoldingQuantity(symbol);
        };

        newPlan = generatePlan(strategy, context, decisions, targetCapital,
                               getQuotePrice, getPositionQuantity, "live");
    } else {
        // Backtest adapter
        auto* exchangeMgr = _server->GetExchangeManager();
        HistorySimulationBase* histExchange = nullptr;
        for (auto type : context.getExchangeTypes()) {
            histExchange = dynamic_cast<HistorySimulationBase*>(
                exchangeMgr->GetExchangeByType(type));
            if (histExchange) break;
        }
        BacktestContext* btContext = histExchange ?
            histExchange->getBacktestContext(context.getBacktestRunId()) : nullptr;

        auto getQuotePrice = [&](symbol_t symbol) -> double {
            if (!histExchange || !btContext) return 0.0;
            uint32_t curIndex = btContext->getCurIndex(symbol);
            if (curIndex > 0) {
                return histExchange->GetPrimitivePrice(symbol, curIndex - 1);
            }
            return 0.0;
        };

        auto getPositionQuantity = [&](symbol_t symbol) -> int64_t {
            if (!histExchange) return 0;
            return histExchange->GetPositionQuantity(symbol);
        };

        newPlan = generatePlan(strategy, context, decisions, targetCapital,
                               getQuotePrice, getPositionQuantity, "backtest");
    }

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

        switch (_sizing_method) {
            case SizingMethod::Equal: meta["sizing_method"] = "equal"; break;
            case SizingMethod::Kelly: meta["sizing_method"] = "kelly"; break;
            case SizingMethod::VolatilityTarget: meta["sizing_method"] = "volatility_target"; break;
            case SizingMethod::Strength: meta["sizing_method"] = "strength"; break;
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
                if (_server->GetRunningMode() == RuningType::Backtest) {
                    INFO("Symbol {} already has position {}, skipping buy",
                         get_symbol(item._symbol), currentQty);
                }
                continue;
            } else {
                // 无仓，BUY = 开多
                int quantity = static_cast<int>(getSymbolCapital(symbol) / price / 100) * 100;
                if (quantity < 100) {
                    if (_server->GetRunningMode() != RuningType::Backtest) {
                        STRATEGY_WARN(strategy, "Quantity {} too small for symbol {}",
                                      quantity, get_symbol(item._symbol));
                    } else {
                        WARN("Quantity {} too small for symbol {}", quantity, get_symbol(item._symbol));
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
                if (_server->GetRunningMode() == RuningType::Backtest) {
                    INFO("[PortfolioNode] SELL action for {}, currentQty={}, price={:.2f}, allowShort={}",
                         get_symbol(item._symbol), currentQty, price, _allowShort);
                    INFO("[PortfolioNode] -> SELL {} added to plan: qty={}, price={:.2f}",
                         get_symbol(item._symbol), currentQty, price);
                }
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
                    if (_server->GetRunningMode() == RuningType::Backtest) {
                        INFO("[PortfolioNode] -> SHORT {} added to plan: qty={}, price={:.2f}",
                             get_symbol(item._symbol), quantity, price);
                    }
                } else {
                    // 预算不足一手时开空会被丢弃。这里必须显式告警：BUY 分支至少有
                    // "Quantity 0 too small" 可查，空头此前完全静默 —— 决策集里只会看到
                    // 上一根 bar 的旧值，排查时看不出是资金口径问题。
                    if (_server->GetRunningMode() != RuningType::Backtest) {
                        STRATEGY_WARN(strategy, "Short quantity {} too small for symbol {} "
                                      "(budget={:.0f}, price={:.2f})",
                                      quantity, get_symbol(item._symbol),
                                      getSymbolCapital(symbol), price);
                    } else {
                        WARN("Short quantity {} too small for symbol {} (budget={:.0f}, price={:.2f})",
                             quantity, get_symbol(item._symbol),
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
                if (_server->GetRunningMode() == RuningType::Backtest) {
                    INFO("[PortfolioNode] -> SELL {} skipped: currentQty={}, allowShort={}",
                         get_symbol(item._symbol), currentQty, _allowShort);
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

// ── 风控短路：RiskContext.triggered 为 true 时，全部改为 SELL ──
void PortfolioNode::applyRiskContext(DataContext& context, Map<symbol_t, TradeAction>& decisions) {
    auto* rc = context.GetRiskContext();
    if (!rc || !rc->triggered) {
        return;
    }

    if (_server->GetRunningMode() != RuningType::Backtest) {
        STRATEGY_INFO(context.CurrentStrategy(), "[PortfolioNode] RiskContext triggered (type={})",
             to_string(rc->trigger_type));
    } else {
        INFO("[PortfolioNode] RiskContext triggered (type={})", to_string(rc->trigger_type));
    }
    for (auto& [symbol, action] : decisions) {
        if (action != TradeAction::SELL) {
            action = TradeAction::SELL;
        }
    }
}

// ── 按 sizing_method 调整仓位权重 ──
void PortfolioNode::applySizingWeights(DataContext& context, Map<symbol_t, TradeAction>& decisions, double targetCapital) {
    // 默认 Equal 模式：不改变现有行为
    if (_sizing_method == SizingMethod::Equal) {
        return;
    }

    // 获取可用标的列表（仅 BUY 信号）
    Vector<symbol_t> buySymbols;
    for (const auto& [symbol, action] : decisions) {
        if (action == TradeAction::BUY) {
            buySymbols.push_back(symbol);
        }
    }
    if (buySymbols.empty()) {
        return;
    }

    Map<symbol_t, double> weights;

    if (_sizing_method == SizingMethod::Kelly) {
        // Kelly 公式: w* = (bp - q) / (b * σ²)
        // 简化版：使用历史胜率估算
        for (const auto& symbol : buySymbols) {
            String key = get_symbol(symbol) + ".win_rate";
            if (context.exist(key)) {
                auto wr_var = context.get<Vector<double>>(key);
                if (!wr_var.empty()) {
                    double win_rate = wr_var.back();
                    double avg_win = 0.02;  // TODO: 从上下文读取
                    double avg_loss = 0.02; // TODO: 从上下文读取
                    if (avg_loss > 0) {
                        double b = avg_win / avg_loss;
                        double q = 1.0 - win_rate;
                        double kelly_pct = (b * win_rate - q) / b;
                        weights[symbol] = std::max(0.0, kelly_pct);
                    }
                }
            } else {
                // 无数据，等权分配
                weights[symbol] = 1.0 / buySymbols.size();
            }
        }
    } else if (_sizing_method == SizingMethod::VolatilityTarget) {
        // 目标波动率: w = vol_target / σ
        for (const auto& symbol : buySymbols) {
            String key = get_symbol(symbol) + ".volatility";
            if (context.exist(key)) {
                auto vol_var = context.get<Vector<double>>(key);
                if (!vol_var.empty()) {
                    double sigma = vol_var.back();
                    if (sigma > 0) {
                        weights[symbol] = _vol_target / sigma;
                    }
                }
            } else {
                weights[symbol] = 1.0 / buySymbols.size();
            }
        }
    } else if (_sizing_method == SizingMethod::Strength) {
        // 信号强度加权: 读取 {symbol}.strength → 归一化
        // strength 值由上游节点写入（XGBoost/Signal/Formula 等均可）
        // 只取正值（负值表示做空信号，不在 BUY 列表中）
        double total_strength = 0.0;
        for (const auto& symbol : buySymbols) {
            String key = get_symbol(symbol) + ".strength";
            double strength = 0.0;
            if (context.exist(key)) {
                auto s_var = context.get<Vector<double>>(key);
                if (!s_var.empty()) {
                    strength = s_var.back();
                }
            }
            // 负值或零值给一个最小权重，避免完全排除
            strength = std::max(strength, 1e-6);
            weights[symbol] = strength;
            total_strength += strength;
        }
        // 归一化
        if (total_strength > 0) {
            for (auto& [symbol, w] : weights) {
                w /= total_strength;
            }
        } else {
            // 全部为零时回退等权
            for (const auto& symbol : buySymbols) {
                weights[symbol] = 1.0 / buySymbols.size();
            }
        }
    }

    if (weights.empty()) {
        return;
    }

    // 截断：单标的上限
    double total_weight = 0.0;
    for (auto& [symbol, w] : weights) {
        w = std::min(w, _max_single_pct);
        total_weight += w;
    }

    // 截断：总仓位上限
    if (total_weight > _max_total_pct) {
        double scale = _max_total_pct / total_weight;
        for (auto& [symbol, w] : weights) {
            w *= scale;
        }
    }

    // 存入成员变量，供 generatePlan 使用
    _weights = std::move(weights);
}

const nlohmann::json PortfolioNode::getParams() {
    return {"positionRatio", "pool", "allowShort", "sizing_method", "max_single_pct", "max_total_pct", "volatility_target", "strength"};
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
