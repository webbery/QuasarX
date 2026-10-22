#include "Nodes/SignalNode.h"
#include "DataContext.h"
#include "ExchangeManager.h"
#include "Interprecter/Stmt.h"
#include "Util/log.h"
#include "Util/system.h"
#include "Util/FinanceDB.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Nodes/QuoteNode.h"
#include <utility>
#include "boost/algorithm/string.hpp"

SignalNode::SignalNode(Server* server):_server(server), _buyParser(nullptr), _sellParser(nullptr) {
}

bool SignalNode::Init(const nlohmann::json& config) {
    auto& buySignal = config["params"]["buy"]["value"];
    auto& sellSignal = config["params"]["sell"]["value"];

    // 保存原始表达式（用于日志）
    _buyExpression = (String)buySignal;
    _sellExpression = (String)sellSignal;

    // 收集可用变量列表及其类型（从输入节点）
    Map<String, ArgType> availableVars;
    for (auto& item: _ins) {
        auto names = item.second->out_elements();
        for (auto& info: names) {
            // info.first 是完整 key 如 "bj.920108.ma_short"
            // info.second 是 ArgType
            availableVars[info.first] = info.second;
        }
    }

    // BFS 上游拿到 _pools,提前到 validate() 之前。
    // 这样 validate() 才能按 short-name ("close") 正确解析 availableVars,
    // 不再因为 key 名不匹配而把类型校验静默放行。
    Set<symbol_t> upstreamSymbols = discoverUpstreamSymbols();

    if (upstreamSymbols.empty()) {
        WARN("[SignalNode:{}] No upstream symbols found from QuoteInputNode, signal node will be skipped", _id);
        return false;
    }

    for (const auto& sym : upstreamSymbols) {
        _pools.emplace_back(sym);
    }

    INFO("[SignalNode:{}] Found {} symbols from upstream", _id, _pools.size());

    // 解析/验证阶段打点：validate() 成功时是静默的，Init 中途抛异常时
    // 只剩一句 e.what()（如 "map::at"），无法判断断在哪个阶段。
    DEBUG_INFO("[SignalNode:{}] stage: parse buy", _id);

    // 解析并验证买入公式
    if (!_buyParser) {
        _buyParser = new FormulaParser(_server);
    }
    if (!_buyParser->parse(buySignal, TradeAction::BUY)) {
        WARN("parse buy express fail.");
        delete _buyParser;
        _buyParser = nullptr;
        throw std::runtime_error("Failed to parse buy signal expression");
    }
    DEBUG_INFO("[SignalNode:{}] stage: validate buy", _id);
    // 新增：类型验证 (带 symbols 以便 short-name 正确解析)
    if (!_buyParser->validate(availableVars, _pools)) {
        std::string error = "Buy signal expression type validation failed: " +
                           _buyParser->getValidationError();
        FATAL("{}", error);
        // 同步推给前端：策略初始化发生在回测请求里，只回 HTTP 500 的话前端只能
        // 看到一句泛化的 "Failed to initialize strategy"，定位不到具体是哪个变量。
        strategy_error("", error);
        delete _buyParser;
        _buyParser = nullptr;
        throw std::runtime_error(error);
    }

    DEBUG_INFO("[SignalNode:{}] stage: parse sell", _id);

    // 解析并验证卖出公式
    if (!_sellParser) {
        _sellParser = new FormulaParser(_server);
    }
    if (!_sellParser->parse(sellSignal, TradeAction::SELL)) {
        WARN("parse sell express fail.");
        delete _sellParser;
        _sellParser = nullptr;
        throw std::runtime_error("Failed to parse sell signal expression");
    }
    DEBUG_INFO("[SignalNode:{}] stage: validate sell", _id);
    // 新增：类型验证 (带 symbols 以便 short-name 正确解析)
    if (!_sellParser->validate(availableVars, _pools)) {
        std::string error = "Sell signal expression type validation failed: " +
                           _sellParser->getValidationError();
        FATAL("{}", error);
        strategy_error("", error);
        delete _sellParser;
        _sellParser = nullptr;
        throw std::runtime_error(error);
    }

    if (config["params"].contains("allowShort")) {
        _allowShort = config["params"]["allowShort"]["value"];
    }

    // 如果允许做空，确保可做空标的列表已加载
    if (_allowShort) {
        ensureMarginStockData();
    }

    return true;
}

void SignalNode::ensureMarginStockData() {
    // 只读：margin_stock 是全局数据，由 Server::RefreshMarginStockList 在每周六
    // 的日终时点刷新，策略不负责拉取。此前在这里同步调 fetch_shortable_stocks.py
    // 有三个问题：
    //   1. 全量拉交易所名单（实测 1681 只 / 6 分钟）只为回答本策略几个标的的成员资格；
    //   2. RunCommand 同步阻塞在 Init 里，失败还静默降级，排查时只有一行 WARN；
    //   3. 全局数据的生命周期挂在单策略上，每个策略启动都可能触发一次网络阻塞。
    // 现在读不到就明确告警并禁用做空，刷新动作交给定时任务。
    auto& financeDB = FinanceDB::instance();
    DEBUG_INFO("[SignalNode:{}] margin: FinanceDB initialized={}", _id,
               financeDB.isInitialized());

    if (!financeDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!financeDB.init(db_path + "/finance", "finance.db")) {
            WARN("[SignalNode:{}] 无法初始化 FinanceDB，禁用做空。"
                 "请确认 {} 是否可写", _id, db_path + "/finance/finance.db");
            _allowShort = false;
            return;
        }
    }

    auto result = financeDB.queryMarginStocks();
    size_t total = result.contains("data") && result["data"].is_array()
                   ? result["data"].size() : 0;
    DEBUG_INFO("[SignalNode:{}] margin: {} rows in table", _id, total);

    if (total == 0) {
        WARN("[SignalNode:{}] margin_stock 为空，禁用做空（策略将退化为纯做多）。"
             "名单由 Server::RefreshMarginStockList 每周六日终刷新；"
             "如需立即生效请手工执行 tools/fetch_shortable_stocks.py 并导入 FinanceDB",
             _id);
        _allowShort = false;
        return;
    }

    for (const auto& sym : _pools) {
        String symStr = get_symbol(sym);
        bool isMargin = financeDB.isMarginStock(symStr);
        DEBUG_INFO("[SignalNode:{}] margin: {} isMarginStock={}", _id, symStr, isMargin);
        if (isMargin) {
            _shortableSymbols.insert(sym);
        }
    }

    if (_shortableSymbols.empty()) {
        WARN("[SignalNode:{}] 标的池 {} 只中无一可做空（margin_stock 共 {} 只），"
             "本策略不会有空头仓位", _id, _pools.size(), total);
    } else {
        INFO("[SignalNode:{}] 标的池中可做空 {}/{}（margin_stock 共 {} 只）",
             _id, _shortableSymbols.size(), _pools.size(), total);
    }
}

NodeProcessResult SignalNode::Process(const String& strategy, DataContext& context)
{
    // INFO("[SignalNode:{}] Process ENTER, pools={}, buy='{}', sell='{}'",
    //      _id, _pools.size(), _buyExpression, _sellExpression);

    Set<String> args;
    for (auto& item: _ins) {
        auto names = item.second->out_elements();
        for (auto& info: names) {
            args.insert(info.first);
        }
    }

    // INFO("[SignalNode:{}] input args ({}): {}",
    //      _id, args.size(), boost::algorithm::join(args, ", "));

    auto buys = _buyParser->envoke(_pools, args, context);
    auto sells = _sellParser->envoke(_pools, args, context);

    // 诊断日志：记录 raw_strength 值（每 100 个 epoch 记录一次，避免日志过多）
    // static int epoch_counter = 0;
    // if (++epoch_counter % 100 == 0) {
    //     for (const auto& symbol : _pools) {
    //         String key = get_symbol(symbol) + ".raw_strength";
    //         if (context.exist(key)) {
    //             auto s_var = context.get<Vector<double>>(key);
    //             if (!s_var.empty()) {
    //                 double strength = s_var.back();
    //                 DEBUG_INFO("[SignalNode:{}] epoch={} {} raw_strength={:.6f}", 
    //                      _id, epoch_counter, get_symbol(symbol), strength);
    //             }
    //         }
    //     }
    // }

    // 如果不允许做空，过滤无持仓标的的 SELL 信号
    Map<symbol_t, int64_t> heldSymbols;
    if (_server->GetRunningMode() == RuningType::Backtest) {
        auto* exchangeMgr = _server->GetExchangeManager();
        for (const auto& symbol : _pools) {
            auto* histExchange = dynamic_cast<HistorySimulationBase*>(exchangeMgr->ResolveExchange(symbol));
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
    if (!_allowShort) {
        // 不允许做空：过滤所有无持仓标的的 SELL 信号
        for (auto it = sells.begin(); it != sells.end(); ++it) {
            if (it->second == TradeAction::SELL && !heldSymbols.count(it->first)) {
                it->second = TradeAction::HOLD;
            }
        }
    } else {
        // 允许做空：只允许对可做空标的（融资融券标的）开空仓
        for (auto it = sells.begin(); it != sells.end(); ++it) {
            if (it->second == TradeAction::SELL && !heldSymbols.count(it->first)) {
                // 无持仓 + SELL = 尝试开空，检查是否可做空
                if (!_shortableSymbols.count(it->first)) {
                    it->second = TradeAction::HOLD;
                }
            }
        }
    }
    // 如果已经持仓,不再追加买入
    for (auto& item: buys) {
        if (item.second == TradeAction::BUY && heldSymbols.count(item.first)) {
            item.second = TradeAction::HOLD;
        }
    }
    // for (auto it = buys.begin(); it != buys.end(); ++it) {
    //     if (it->second == TradeAction::BUY) INFO("[SignalNode] {} BUY signal, held={}", get_symbol(it->first), heldSymbols.count(it->first) ? heldSymbols[it->first] : 0);
    // }
    // for (auto it = sells.begin(); it != sells.end(); ++it) {
    //     if (it->second == TradeAction::SELL) INFO("[SignalNode] {} SELL signal, held={}", get_symbol(it->first), heldSymbols.count(it->first) ? heldSymbols[it->first] : 0);
    // }
    Map<symbol_t, TradeAction> decisions;
    for (auto& trade: {buys, sells}) {
        for (auto& item: trade) {
            if (item.second != TradeAction::HOLD) {
                if (decisions.count(item.first) && decisions[item.first] != item.second) {
                    if (_server->GetRunningMode() != RuningType::Backtest) {
                        STRATEGY_IMPORTANT(strategy, "[SignalNode] {} not match operation!", item.first);
                    } else {
                        IMPORTANT("[SignalNode] {} not match operation!", item.first);
                    }
                    continue;
                }
                decisions[item.first] = item.second;
                TradeSignal *signal = new TradeSignal(item.first, item.second);
                context.AddSignal(signal);
            }
        }
    }

    // 将信号数据写入 context，供 debug 节点使用
    // 信号值：1=买入，-1=卖出，0=持有
    for (auto& symbol : _pools) {
        String key = get_symbol(symbol) + ".signal";
        int signalValue = 0;  // 默认持有
        if (decisions.count(symbol)) {
            if (decisions[symbol] == TradeAction::BUY) {
                signalValue = 1;
            } else if (decisions[symbol] == TradeAction::SELL) {
                signalValue = -1;
            }
        }
        // 检查是否已存在，存在则追加，否则创建新向量
        if (context.exist(key)) {
            context.add(key, (double)signalValue);
        } else {
            // 首次写入，预填充 warmup 占位符
            // warmup 期间 SignalNode 被跳过，但 QuoteInputNode 仍然写入了时间
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

    // ── DuckDB node_io 日志（仅实盘模式）──
    NODE_IO_LOG("signal", _id,
        input["buy_expression"] = _buyExpression;
        input["sell_expression"] = _sellExpression;
        nlohmann::json pools_arr = nlohmann::json::array();
        for (const auto& sym : _pools) {
            pools_arr.push_back(get_symbol(sym));
        }
        input["pools"] = pools_arr;

        nlohmann::json sig_arr = nlohmann::json::array();
        for (const auto& symbol : _pools) {
            String key = get_symbol(symbol) + ".signal";
            int signal_value = 0;
            if (context.exist(key)) {
                const auto& sig_vec = context.get<Vector<double>>(key);
                if (!sig_vec.empty()) signal_value = static_cast<int>(sig_vec.back());
            }
            nlohmann::json sig_entry;
            sig_entry["symbol"] = get_symbol(symbol);
            sig_entry["signal"] = signal_value;
            sig_arr.push_back(sig_entry);
        }
        output["signals"] = sig_arr;
    );

    return NodeProcessResult::Success;
}

bool SignalNode::ParseBuyExpression(const String& expression) {
    if (!_buyParser) {
        _buyParser = new FormulaParser(_server);
    }
    return _buyParser->parse(expression, TradeAction::BUY);
}

bool SignalNode::ParseSellExpression(const String& expression) {
    if (!_sellParser) {
        _sellParser = new FormulaParser(_server);
    }
    return _sellParser->parse(expression, TradeAction::SELL);
}

SignalNode::~SignalNode() {
    if (_buyParser) {
        delete _buyParser;
    }
    if (_sellParser) {
        delete _sellParser;
    }
}

const nlohmann::json SignalNode::getParams() {
    return {"buy", "sell", "allowShort"};
}

Map<String, ArgType> SignalNode::out_elements() {
    Map<String, ArgType> elems;
    // 输出信号数据，格式为 "{symbol}.signal"
    for (auto& symbol : _pools) {
        // 信号是整数时间序列
        elems[get_symbol(symbol) + ".signal"] = ArgType::Integer_TimeSeries;
    }
    return elems;
}