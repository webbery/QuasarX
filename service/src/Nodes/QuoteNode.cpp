#include "Nodes/QuoteNode.h"
#include "ExchangeManager.h"
#include "StrategyNode.h"
#include "Util/string_algorithm.h"
#include "Util/log.h"
#include "Bridge/ETFOptionSymbol.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Bridge/SIM/ETFHistorySimulation.h"
#include "Util/system.h"
#include <ctime>
#include <limits>
#include <stdexcept>
#include "server.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"

QuoteInputNode::QuoteInputNode(Server* server): _server(server) {
}

/**
 * @brief 从 QuoteInfo 提取指定属性值
 */
double QuoteInputNode::getProp(const QuoteInfo& quote, const String& property) const {
    if (property == "open") return quote._open;
    if (property == "close") return quote._close;
    if (property == "high") return quote._high;
    if (property == "low") return quote._low;
    if (property == "volume") return (double)quote._volume;
    return 0.0;
}

/**
 * @brief 将单个 symbol 的行情数据写入 context
 *
 * 默认语义: close/open/high/low = 后复权价格（指标/XGBoost 与训练数据一致），
 * 原始价格另存 org_close/org_open/org_high/org_low（撮合等需要原始价的场景用）。
 * 后复权数据缺失（=0）时回退写原始价，避免空值污染下游。
 */
void QuoteInputNode::writeQuote(DataContext& context, const QuoteInfo& quote) {
    auto name = get_symbol(quote._symbol);
    auto baseKey = name + ".";
    for (auto& property : _properties[name]) {
        if (property == "open") {
            addQuoteProperty(context, baseKey + property, quote._adj_open > 0.0 ? quote._adj_open : quote._open);
            addQuoteProperty(context, baseKey + "org_open", quote._open);
        } else if (property == "close") {
            addQuoteProperty(context, baseKey + property, quote._adj_close > 0.0 ? quote._adj_close : quote._close);
            addQuoteProperty(context, baseKey + "org_close", quote._close);
        } else if (property == "high") {
            addQuoteProperty(context, baseKey + property, quote._adj_high > 0.0 ? quote._adj_high : quote._high);
            addQuoteProperty(context, baseKey + "org_high", quote._high);
        } else if (property == "low") {
            addQuoteProperty(context, baseKey + property, quote._adj_low > 0.0 ? quote._adj_low : quote._low);
            addQuoteProperty(context, baseKey + "org_low", quote._low);
        } else {
            // volume 无复权概念，其他属性原样写
            addQuoteProperty(context, baseKey + property, getProp(quote, property));
        }
    }
    // 同步到 QuoteInfo 缓存，供其他模块通过 context.GetQuote() 获取
    context.SetQuote(quote._symbol, quote);
}

void QuoteInputNode::addQuoteProperty(DataContext& context, const String& key, double val) {
    if (context.exist(key)) {
        context.add(key, val);
    } else {
        context.add(key, Vector<double>{val});
    }
}

bool QuoteInputNode::Init(const nlohmann::json& config) {
    auto& codes = config["params"]["code"]["value"];
    QuoteFilter filer;
    for (String code: codes) {
        // TODO: ALL_STOCK / ALL_ETF — 从 TickFlow API 获取全市场标的列表后展开
        if (code == "ALL_STOCK" || code == "ALL_ETF") {
            WARN("QuoteInputNode: {} not yet implemented (requires TickFlow full market symbol list)", code);
            continue;
        }
        auto symbol = to_symbol(code);
        if (is_null(symbol)) {
            WARN("QuoteInputNode: invalid symbol code '{}'", code);
            continue;
        }
        _symbols.insert(symbol);
        filer._symbols.emplace(code);
    }

    // 默认输出字段：当前节点输出的所有行情属性
    // 注：_outs 的 key 是 sourceHandle（前端生成 "output"），不是字段名；
    //     单纯依赖 _outs 解析字段名会让 _properties 为空导致 out_elements() 返回 0。
    //     这里默认填充标准字段（open/close/high/low/volume/turnover），覆盖前端单 output handle 场景。
    static const Set<String> default_props = {
        "open", "close", "high", "low", "volume", "turnover"
    };

    Set<String> visited_propers;
    bool has_prop_spec = false;
    for (auto& item: _outs) {
        auto& handle = item.first;
        if (visited_propers.count(handle)) continue;
        visited_propers.insert(handle);
        Vector<String> froms;
        split(handle, froms, "-");
        if (froms.size() == 2) {
            // prop-spec 格式 handle（如 "output-close"）— 提取字段名
            for (auto itr = _symbols.begin(); itr != _symbols.end(); ++itr) {
                _properties[get_symbol(*itr)].insert(froms[1]);
            }
            has_prop_spec = true;
        }
    }

    // 默认字段填充（prop-spec 缺失或 _outs 为空时）
    if (!has_prop_spec) {
        for (auto itr = _symbols.begin(); itr != _symbols.end(); ++itr) {
            _properties[get_symbol(*itr)].insert(default_props.begin(), default_props.end());
        }
    }

    // 读取频率设置（回测和实盘模式都需要）
    String freqStr = "1d";
    if (config["params"].contains("freq")) {
        freqStr = (String)config["params"]["freq"]["value"];
        if (freqStr == "1m") _freq = DataFrequencyType::Min1;
        else if (freqStr == "5m") _freq = DataFrequencyType::Min5;
        else _freq = DataFrequencyType::Day;  // "1d" 或其他
    }

    // 回测模式专属逻辑
    if (_server->GetRunningMode() == RuningType::Backtest) {
        // 解析数据源（前端 multiselect 传数组）
        _sources.insert(contract_type::stock); // 默认股票
        if (config["params"].contains("source")) {
            _sources.clear();
            auto& srcVal = config["params"]["source"]["value"];
            for (auto& s : srcVal) {
                String type = (String)s;
                if (type == "股票") _sources.insert(contract_type::stock);
                else if (type == "ETF") _sources.insert(contract_type::exchange_traded_fund);
                else if (type == "期权") _sources.insert(contract_type::option);
                else if (type == "期货") _sources.insert(contract_type::future);
            }
        }
        if (_sources.empty()) _sources.insert(contract_type::stock);

        auto* exchangeMgr = _server->GetExchangeManager();

        // 按 source 类型初始化对应 Exchange
        if (_sources.count(contract_type::exchange_traded_fund) && exchangeMgr) {
            exchangeMgr->EnsureExchangeByType(ExchangeType::EX_ETF_HIST_SIM);

            auto* etfExchange = dynamic_cast<HistorySimulationBase*>(
                exchangeMgr->GetExchangeByType(ExchangeType::EX_ETF_HIST_SIM));
            if (etfExchange) {
                etfExchange->SetFilter(filer);
                auto* etfHist = dynamic_cast<ETFHistorySimulation*>(etfExchange);
                if (etfHist) etfHist->UseFreq(freqStr);
            }
        }
        if (_sources.count(contract_type::stock) && exchangeMgr) {
            exchangeMgr->EnsureExchangeByType(ExchangeType::EX_STOCK_HIST_SIM);

            auto* exchange = dynamic_cast<HistorySimulationBase*>(
                _server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM));
            if (exchange) {
                exchange->SetFilter(filer);
                auto* stockHist = dynamic_cast<StockHistorySimulation*>(exchange);
                if (stockHist) {
                    if (freqStr == "1d") {
                        stockHist->UseLevel(TradingMode::T1);
                    } else {
                        stockHist->UseLevel(TradingMode::T0);
                        stockHist->SetT0Freq(freqStr);
                    }
                }
            }
        }
    }

    // 读取缺失数据处理方式并创建对应策略
    if (config["params"].contains("missingHandle")) {
        String mode = config["params"]["missingHandle"]["value"];
        if (mode == "linear") {
            _missingHandle = MissingHandleType::Linear;
        } else if (mode == "forward") {
            _missingHandle = MissingHandleType::ForwardFill;
        } else if (mode == "backward") {
            _missingHandle = MissingHandleType::BackwardFill;
        } else {
            _missingHandle = MissingHandleType::Skip;
        }
    }
    _fillStrategy = CreateFillStrategy(_missingHandle);
    
    return true;
}

void QuoteInputNode::Prepare(const String& strategy, DataContext& context) {
    // 根据数据源注册 Exchange 类型，供 PortfolioNode/ExecuteNode 后续使用
    if (_sources.count(contract_type::exchange_traded_fund)) {
        context.addExchangeType(ExchangeType::EX_ETF_HIST_SIM);
    }
    if (_sources.count(contract_type::stock)) {
        context.addExchangeType(ExchangeType::EX_STOCK_HIST_SIM);
    }
    if (_sources.count(contract_type::option)) {
        // TODO: 期权回测引擎 EX_OPTION_HIST_SIM
    }
}

NodeProcessResult QuoteInputNode::Process(const String& strategy, DataContext& context) {
    // 实盘模式：QuoteInfo 已由引擎通过 KBarBuilder 写入 context
    if (_server->GetRunningMode() != RuningType::Backtest) {
        bool anyQuote = false;
        time_t min_t = std::numeric_limits<time_t>::max();
        for (auto& symbol : _symbols) {
            const QuoteInfo* q = context.GetQuote(symbol);
            if (q && q->_time > 0) {
                _curQuotes[symbol] = *q;
                writeQuote(context, *q);
                _lastQuotes[symbol] = *q;
                if (q->_time < min_t) min_t = q->_time;
                anyQuote = true;
            }
        }
        if (anyQuote) {
            context.SetTime(min_t);

            // 实盘模式：记录节点 IO 日志到 DuckDB
            NODE_IO_LOG_FAST("input", _id,
                nlohmann::json quotes = nlohmann::json::array();
                for (auto& [sym, q] : _curQuotes) {
                    nlohmann::json item;
                    item["symbol"] = get_symbol(sym);
                    item["open"] = q._open;
                    item["high"] = q._high;
                    item["low"] = q._low;
                    item["close"] = q._close;
                    item["volume"] = q._volume;
                    item["time"] = static_cast<int64_t>(q._time);
                    quotes.push_back(item);
                }
                nlohmann::json keys = nlohmann::json::array();
                for (auto& [sym, props] : _properties) {
                    for (auto& p : props) {
                        keys.push_back(sym + "." + p);
                    }
                }
                output["quotes"] = quotes;
                output["keys_written"] = keys;
                output["aligned"] = true;
            );

            return NodeProcessResult::Success;
        }
        return NodeProcessResult::Skip;
    }

    // 回测模式：按 symbol 类型路由到对应 Exchange
    run_id_t runId = context.getBacktestRunId();
    auto* exchangeMgr = _server->GetExchangeManager();

    // 第一步：收集所有 symbol 当前 bar 的 quote，同时找出最小时间戳
    time_t min_t = std::numeric_limits<time_t>::max();
    for (auto& symbol : _symbols) {
        auto* exch = dynamic_cast<HistorySimulationBase*>(
            exchangeMgr->ResolveExchange(symbol));
        if (!exch) return NodeProcessResult::Skip;

        QuoteInfo quote = exch->GetQuote(symbol, runId);
        // time == 0 表示该 symbol 数据已用完
        if (quote._time == 0) return NodeProcessResult::Finished;

        _curQuotes[symbol] = quote;
        if (quote._time < min_t) min_t = quote._time;
    }
    // 第二步：检查是否所有 symbol 时间戳一致
    bool allAligned = true;
    for (auto& [symbol, quote] : _curQuotes) {
        if (quote._time != min_t) {
            allAligned = false;
            break;
        }
    }

    // 第三步：对齐则直接写入，返回 Success
    if (allAligned) {
        for (auto& [symbol, quote] : _curQuotes) {
            writeQuote(context, quote);
            _lastQuotes[symbol] = quote;
        }
        context.SetTime(min_t);
        return NodeProcessResult::Success;
    }

    // 第四步：不对齐则使用填充策略处理
    bool success = _fillStrategy->alignAndWrite(
        _curQuotes,
        min_t,
        _lastQuotes,
        context,
        [this, &context](const QuoteInfo& q) {
            writeQuote(context, q);
        }
    );

    if (success) {
        for (auto& [symbol, quote] : _curQuotes) {
            _lastQuotes[symbol] = quote;
        }
        context.SetTime(min_t);
        return NodeProcessResult::Success;
    }

    // Skip 策略返回 false，跳过本轮
    return NodeProcessResult::Skip;
}

Map<String, ArgType> QuoteInputNode::out_elements() {
    Map<String, ArgType> names;
    
    // 调试日志：打印 _symbols 和 _properties 的状态
    DEBUG_INFO("[QuoteInputNode:{}] out_elements() called: _symbols size = {}, _properties size = {}", 
         _id, _symbols.size(), _properties.size());
    
    for (auto itr = _symbols.begin(); itr != _symbols.end(); ++itr) {
        auto name = get_symbol(*itr);
        auto baseKey = name + ".";
        
        DEBUG_INFO("[QuoteInputNode:{}] out_elements: symbol='{}', _properties['{}'] size = {}", 
             _id, *itr, name, _properties[name].size());
        
        for (auto& item: _properties[name]) {
            if (item == "volume") {
                names[baseKey + item] = ArgType::Integer_TimeSeries;
            } else {
                names[baseKey + item] = ArgType::Double_TimeSeries;   // 默认后复权
                names[baseKey + "org_" + item] = ArgType::Double_TimeSeries;  // 原始价
            }
        }
    }
    
    DEBUG_INFO("[QuoteInputNode:{}] out_elements returning {} elements", _id, names.size());
    return names;
}

const nlohmann::json QuoteInputNode::getParams() {
    return {"code", "freq", "missingHandle"};
}
