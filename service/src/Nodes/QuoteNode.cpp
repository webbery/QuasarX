#include "Nodes/QuoteNode.h"
#include "ExchangeManager.h"
#include "StrategyNode.h"
#include "Util/string_algorithm.h"
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
 */
void QuoteInputNode::writeQuote(DataContext& context, const QuoteInfo& quote) {
    auto name = get_symbol(quote._symbol);
    auto baseKey = name + ".";
    for (auto& property : _properties[name]) {
        auto key = baseKey + property;
        addQuoteProperty(context, key, getProp(quote, property));
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
        auto& security = Server::GetSecurity(code);
        auto symbol = to_symbol(code, security);
        _symbols.insert(symbol);
        filer._symbols.emplace(code);
    }

    Set<String> visited_propers;
    for (auto& item: _outs) {
        auto& handle = item.first;
        if (visited_propers.count(handle)) continue;
        visited_propers.insert(handle);
        Vector<String> froms;
        split(handle, froms, "-");
        if (froms.size() == 2) {
            for (auto itr = _symbols.begin(); itr != _symbols.end(); ++itr) {
                _properties[get_symbol(*itr)].insert(froms[1]);
            }
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
        _source = "股票";
        if (config["params"].contains("source")) {
            _source = (String)config["params"]["source"]["value"];
        }

        auto* exchangeMgr = _server->GetExchangeManager();

        if (_source == "ETF" && exchangeMgr) {
            exchangeMgr->EnsureExchangeByType(ExchangeType::EX_ETF_HIST_SIM);

            auto* etfExchange = dynamic_cast<HistorySimulationBase*>(
                exchangeMgr->GetExchangeByType(ExchangeType::EX_ETF_HIST_SIM));
            if (etfExchange) {
                etfExchange->SetFilter(filer);
                auto* etfHist = dynamic_cast<ETFHistorySimulation*>(etfExchange);
                if (etfHist) etfHist->UseFreq(freqStr);
            }
        } else {
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
    if (_source == "ETF") {
        context.addExchangeType(ExchangeType::EX_ETF_HIST_SIM);
    } else {
        context.addExchangeType(ExchangeType::EX_STOCK_HIST_SIM);
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
    INFO("[QuoteInputNode:{}] out_elements() called: _symbols size = {}, _properties size = {}", 
         _id, _symbols.size(), _properties.size());
    
    for (auto itr = _symbols.begin(); itr != _symbols.end(); ++itr) {
        auto name = get_symbol(*itr);
        auto baseKey = name + ".";
        
        INFO("[QuoteInputNode:{}] out_elements: symbol='{}', _properties['{}'] size = {}", 
             _id, *itr, name, _properties[name].size());
        
        for (auto& item: _properties[name]) {
            if (item == "volume") {
                names[baseKey + item] = ArgType::Integer_TimeSeries;
            } else {
                names[baseKey + item] = ArgType::Double_TimeSeries;
            }
        }
    }
    
    INFO("[QuoteInputNode:{}] out_elements returning {} elements", _id, names.size());
    return names;
}

const nlohmann::json QuoteInputNode::getParams() {
    return {"code", "freq", "missingHandle"};
}
