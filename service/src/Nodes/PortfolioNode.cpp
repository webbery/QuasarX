#include "Nodes/PortfolioNode.h"
#include "DataContext.h"
#include "ExchangeManager.h"
#include "StrategyNode.h"
#include "Util/system.h"
#include "server.h"
#include "Bridge/exchange.h"
#include "Util/log.h"
#include "PortfolioSubsystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Nodes/SignalNode.h"
#include "std_header.h"
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

    // 交易池 - 优先从 config 读取，如果没有则从上游 SignalNode 获取
    if (config["params"].contains("pool")) {
        auto& poolConfig = config["params"]["pool"]["value"];
        for (const String& code : poolConfig) {
            auto& security = Server::GetSecurity(code);
            _pool.insert(to_symbol(code, security));
        }
    } else {
        // 从上游 SignalNode 获取交易池
        for (auto& [port, inputNode] : _ins) {
            if (auto* signalNode = dynamic_cast<SignalNode*>(inputNode)) {
                auto pool = signalNode->GetPool();
                for (const auto& symbol : pool) {
                    _pool.insert(symbol);
                }
            }
        }
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
    double capital = context.getAvailableCapital();
    double targetCapital = capital * _positionRatio;

    // ── 新增：仓位 sizing 调整 ──
    applySizingWeights(context, decisions, targetCapital);

    // 3. 生成执行计划
    ExecutionPlan newPlan;
    if (_server->GetRunningMode() != RuningType::Backtest) { [[likely]]
        newPlan = generatePlan(strategy, context, decisions, targetCapital);
    } else {
        // 回测模式：获取 BacktestContext 以获取正确的 symbol 索引
        BacktestContext* btContext = nullptr;
        auto* exchangeMgr = _server->GetExchangeManager();
        for (auto type : context.getExchangeTypes()) {
            auto* histExchange = dynamic_cast<HistorySimulationBase*>(
                exchangeMgr->GetExchangeByType(type));
            if (histExchange) {
                btContext = histExchange->getBacktestContext(context.getBacktestRunId());
                if (btContext) break;
            }
        }
        newPlan = generatePlan(context, decisions, targetCapital, btContext);
    }

    // 4. 检查是否有变化
    newPlan._hasChanged = isPlanChanged(newPlan);

    // 5. 如果有变化或首次执行，输出执行计划
    if (newPlan._hasChanged || _lastPlan._items.empty()) {
        _lastPlan._items = std::move(newPlan._items);
        _lastPlan._totalCapital = newPlan._totalCapital;
        _lastPlan._usedCapital = newPlan._usedCapital;
        _lastPlan._hasChanged = true;

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

    return NodeProcessResult::Success;
}

ExecutionPlan PortfolioNode::generatePlan(
    const String& strategy,
    DataContext& context,
    const Map<symbol_t, TradeAction>& decisions,
    double targetCapital
) {
    ExecutionPlan plan;
    plan._totalCapital = targetCapital;
    plan._usedCapital = 0.0;
    plan._hasChanged = false;

    if (decisions.empty()) {
        return plan;
    }

    size_t count = decisions.size();
    double perSymbolCapital = targetCapital / count;

    // 从 DataContext 获取策略初始化时设置的 Exchange 类型
    auto* exchangeMgr = _server->GetExchangeManager();
    ExchangeInterface* exchange = nullptr;
    for (auto type : context.getExchangeTypes()) {
        exchange = exchangeMgr->GetExchangeByType(type);
        if (exchange) break;
    }
    // fallback: 尝试股票 Exchange
    if (!exchange) {
        exchange = exchangeMgr->GetExchangeByType(ExchangeType::EX_STOCK_HIST_SIM);
    }

    for (const auto& [symbol, action] : decisions) {
        ExecutionItem item;
        item._symbol = symbol;
        item._action = action;

        QuoteInfo quote = exchange->GetQuote(item._symbol);
        double price = (quote._close > 0) ? quote._close : quote._open;

        if (price <= 0) {
            if (_server->GetRunningMode() != RuningType::Backtest) {
                STRATEGY_WARN(strategy, "Invalid price for symbol {}", get_symbol(item._symbol));
            } else {
                WARN("Invalid price for symbol {}", get_symbol(item._symbol));
            }
            continue;
        }

        if (item._action == TradeAction::HOLD) {
            auto& position = _server->GetPosition("");
            for (const auto& pos : position._positions) {
                if (pos._symbol == item._symbol) {
                    item._quantity = static_cast<int>(pos._holds);
                    item._limitPrice = 0;
                    item._targetValue = item._quantity * price;
                    plan._items.push_back(item);
                    plan._usedCapital += item._targetValue;
                    break;
                }
            }
        } else if (item._action == TradeAction::BUY) {
            auto& position = _server->GetPosition("");
            int currentQty = 0;
            for (const auto& pos : position._positions) {
                if (pos._symbol == item._symbol) {
                    currentQty = static_cast<int>(pos._holds);
                    break;
                }
            }
            if (currentQty < 0) {
                // 有空仓，BUY = 平空
                item._quantity = -currentQty;
                item._flag = 1; // 平仓
                item._limitPrice = 0;
                item._targetValue = item._quantity * price;
                plan._items.push_back(item);
            } else if (currentQty > 0) {
                // 已有多仓，跳过
                continue;
            } else {
                // 无仓，BUY = 开多
                int quantity = static_cast<int>(perSymbolCapital / price / 100) * 100;
                if (quantity < 100) {
                    if (_server->GetRunningMode() != RuningType::Backtest) {
                        STRATEGY_WARN(strategy, "Quantity {} too small for symbol {}", quantity, get_symbol(item._symbol));
                    } else {
                        WARN("Quantity {} too small for symbol {}", quantity, get_symbol(item._symbol));
                    }
                    continue;
                }
                item._quantity = quantity;
                item._flag = 0; // 开仓
                item._limitPrice = 0;
                item._targetValue = quantity * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            }
        } else if (item._action == TradeAction::SELL) {
            auto& position = _server->GetPosition("");
            int currentQty = 0;
            for (const auto& pos : position._positions) {
                if (pos._symbol == item._symbol) {
                    currentQty = static_cast<int>(pos._holds);
                    break;
                }
            }
            if (currentQty > 0) {
                // 平多
                item._quantity = currentQty;
                item._flag = 1; // 平仓
                item._limitPrice = 0;
                item._targetValue = currentQty * price;
                plan._items.push_back(item);
            } else if (currentQty == 0 && _allowShort) {
                // 做空（开仓）
                int quantity = static_cast<int>(perSymbolCapital / price / 100) * 100;
                if (quantity >= 100) {
                    item._quantity = quantity;
                    item._flag = 0; // 开仓
                    item._limitPrice = 0;
                    item._targetValue = quantity * price;
                    plan._items.push_back(item);
                }
            }
        }
    }

    return plan;
}

ExecutionPlan PortfolioNode::generatePlan(DataContext& context, const Map<symbol_t, TradeAction>& decisions,
                              double targetCapital,
                              BacktestContext* btContext) {
    ExecutionPlan plan;
    plan._totalCapital = targetCapital;
    plan._usedCapital = 0.0;
    plan._hasChanged = false;

    if (decisions.empty()) {
        return plan;
    }

    size_t count = decisions.size();
    double perSymbolCapital = targetCapital / count;

    // 从 DataContext 获取策略初始化时设置的 Exchange 类型，用于获取未复权价格
    auto* exchangeMgr = _server->GetExchangeManager();
    HistorySimulationBase* histExchange = nullptr;
    for (auto type : context.getExchangeTypes()) {
        histExchange = dynamic_cast<HistorySimulationBase*>(
            exchangeMgr->GetExchangeByType(type));
        if (histExchange) break;
    }

    for (const auto& [symbol, action] : decisions) {
        ExecutionItem item;
        item._symbol = symbol;
        item._action = action;

        String symbolName = get_symbol(item._symbol);

        // 获取未复权价格（原始价格）用于回测交易
        // 使用 BacktestContext 中每个 symbol 独立的 curIndex，而非全局 epoch
        // stepForward 已经 incrementCurIndex，所以当前 bar 的索引是 curIndex - 1
        double price = 0.0;
        if (histExchange && btContext) {
            uint32_t curIndex = btContext->getCurIndex(item._symbol);
            auto t = context.Current();
            if (curIndex > 0) {
                price = histExchange->GetPrimitivePrice(item._symbol, curIndex - 1);
            }
        }

        if (price <= 0) {
            WARN("Invalid price for symbol {} in backtest context", symbolName);
            continue;
        }

        if (item._action == TradeAction::HOLD) {
            // 保持持仓：从 StockHistorySimulation 获取当前持仓
            int64_t currentQty = 0;
            if (histExchange) {
                currentQty = histExchange->GetPositionQuantity(item._symbol);
            }
            item._quantity = currentQty;
            if (item._quantity > 0) {
                item._limitPrice = 0;
                item._targetValue = item._quantity * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
            }
        } else if (item._action == TradeAction::BUY) {
            int64_t currentQty = 0;
            if (histExchange) {
                currentQty = histExchange->GetPositionQuantity(item._symbol);
            }
            if (currentQty < 0) {
                // 有空仓，BUY = 平空
                item._quantity = static_cast<int>(-currentQty);
                item._flag = 1; // 平仓
                item._limitPrice = price;
                item._targetValue = item._quantity * price;
                plan._items.push_back(item);
            } else if (currentQty > 0) {
                // 已有多仓，跳过
                INFO("Symbol {} already has position {}, skipping buy", symbolName, currentQty);
                continue;
            } else {
                // 无仓，BUY = 开多
                int64_t quantity = static_cast<int64_t>(perSymbolCapital / price / 100) * 100;
                if (quantity < 100) {
                    WARN("Quantity {} too small for symbol {}", quantity, symbolName);
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
            int64_t currentQty = 0;
            if (histExchange) {
                currentQty = histExchange->GetPositionQuantity(item._symbol);
            }
            String symName = get_symbol(item._symbol);
            INFO("[PortfolioNode] SELL action for {}, currentQty={}, price={:.2f}, allowShort={}",
                 symName, currentQty, price, _allowShort);
            if (currentQty > 0) {
                // 平多
                item._quantity = currentQty;
                item._flag = 1; // 平仓
                item._limitPrice = price;
                item._targetValue = currentQty * price;
                plan._items.push_back(item);
                plan._usedCapital += item._targetValue;
                INFO("[PortfolioNode] -> SELL {} added to plan: qty={}, price={:.2f}", symName, currentQty, price);
            } else if (currentQty == 0 && _allowShort) {
                // 做空（开仓）
                int64_t quantity = static_cast<int64_t>(perSymbolCapital / price / 100) * 100;
                if (quantity >= 100) {
                    item._quantity = quantity;
                    item._flag = 0; // 开仓
                    item._limitPrice = price;
                    item._targetValue = quantity * price;
                    plan._items.push_back(item);
                    plan._usedCapital += item._targetValue;
                    INFO("[PortfolioNode] -> SHORT {} added to plan: qty={}, price={:.2f}", symName, quantity, price);
                }
            } else {
                INFO("[PortfolioNode] -> SELL {} skipped: currentQty={}, allowShort={}", symName, currentQty, _allowShort);
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
            oldItem._flag != newItem._flag) {
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

    // 将权重信息写入 context，供 generatePlan 参考
    // TODO: 后续可在 generatePlan 中直接使用 weights 调整 quantity
    (void)weights; // 当前仅计算，实际仓位调整逻辑待后续完善
}

const nlohmann::json PortfolioNode::getParams() {
    return {"positionRatio", "pool", "allowShort", "sizing_method", "max_single_pct", "max_total_pct", "volatility_target"};
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
