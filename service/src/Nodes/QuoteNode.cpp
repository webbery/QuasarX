#include "Nodes/QuoteNode.h"
#include "StrategyNode.h"
#include "Util/string_algorithm.h"
#include "Bridge/ETFOptionSymbol.h"
#include "Util/system.h"
#include <ctime>
#include <limits>
#include <stdexcept>
#include "server.h"
#include "Bridge/SIM/StockHistorySimulation.h"

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
}

void QuoteInputNode::addQuoteProperty(DataContext& context, const String& key, double val) {
    if (context.exist(key)) {
        context.add(key, val);
    } else {
        context.add(key, Vector<double>{val});
    }
}

/**
 * @brief 线性插值：用上一个 bar 和当前 bar 插值到 targetTime 并写入 context
 */
void QuoteInputNode::interpolateAndWrite(DataContext& context, const symbol_t& symbol,
        const QuoteInfo& nextQuote, time_t targetTime) {
    auto it = _lastQuotes.find(symbol);
    if (it == _lastQuotes.end()) return;

    const auto& lastQuote = it->second;
    time_t range = nextQuote._time - lastQuote._time;
    if (range == 0) return;

    double ratio = (double)(targetTime - lastQuote._time) / range;
    auto name = get_symbol(symbol);
    auto baseKey = name + ".";
    for (auto& property : _properties[name]) {
        double v = getProp(lastQuote, property) + (getProp(nextQuote, property) - getProp(lastQuote, property)) * ratio;
        context.add(baseKey + property, v);
        addQuoteProperty(context, baseKey + property, v);
    }
}

/**
 * @brief 前向填充：用上一个已知 bar 的值填充到 targetTime 并写入 context
 */
void QuoteInputNode::forwardFillAndWrite(DataContext& context, const symbol_t& symbol, time_t targetTime) {
    auto it = _lastQuotes.find(symbol);
    if (it == _lastQuotes.end()) return;

    const auto& lastQuote = it->second;
    auto name = get_symbol(symbol);
    auto baseKey = name + ".";
    for (auto& property : _properties[name]) {
        double v = getProp(lastQuote, property);
        addQuoteProperty(context, baseKey + property, v);
    }
}

/**
 * @brief 后向填充：用下一个已知 bar 的值填充到 targetTime 并写入 context
 */
void QuoteInputNode::backwardFillAndWrite(DataContext& context, const QuoteInfo& nextQuote,
        const symbol_t& symbol, time_t targetTime) {
    auto name = get_symbol(symbol);
    auto baseKey = name + ".";
    for (auto& property : _properties[name]) {
        double v = getProp(nextQuote, property);
        addQuoteProperty(context, baseKey + property, v);
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

    // 设置数据源
    if (_server->GetRunningMode() == RuningType::Backtest) {
        StockHistorySimulation* exchange = (StockHistorySimulation*)_server->GetExchange(ExchangeType::EX_STOCK_HIST_SIM);
        exchange->SetFilter(filer);
        String tickLevel = config["params"]["freq"]["value"];
        if (tickLevel == "1d") {
            exchange->UseLevel(1);
        } else {
            exchange->UseLevel(0);
        }
    }

    // 读取缺失数据处理方式
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

    return true;
}

NodeProcessResult QuoteInputNode::Process(const String& strategy, DataContext& context)
{
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

    // 回测模式：从 StockHistorySimulation 获取当前 bar 的行情数据
    auto* exchange = dynamic_cast<StockHistorySimulation*>(_server->GetAvaliableStockExchange());

    // 第一步：收集所有 symbol 当前 bar 的 quote，同时找出最小时间戳
    time_t min_t = std::numeric_limits<time_t>::max();
    bool allAligned = true;

    for (auto& symbol : _symbols) {
        QuoteInfo quote = exchange->GetQuote(symbol, context.getBacktestRunId());
        // time == 0 表示该 symbol 数据已用完
        if (quote._time == 0) return NodeProcessResult::Finished;

        _curQuotes[symbol] = quote;
        // 找出最小时间戳
        if (quote._time < min_t) min_t = quote._time;
    }

    // 检查是否所有 symbol 时间戳一致（与最小时间戳对齐）
    for (auto& [symbol, quote] : _curQuotes) {
        if (quote._time != min_t) {
            allAligned = false;
            break;
        }
    }

    // 第二步：根据模式处理数据
    if (allAligned) {
        // 所有 symbol 时间戳一致，直接写入
        for (auto& [symbol, quote] : _curQuotes) {
            writeQuote(context, quote);
            _lastQuotes[symbol] = quote;
        }
        context.SetTime(min_t);
        return NodeProcessResult::Success;
    }

    // 时间戳不对齐，根据缺失处理模式处理
    if (_missingHandle == MissingHandleType::Linear) {
        for (auto& [symbol, quote] : _curQuotes) {
            if (quote._time == min_t) {
                // 时间戳对齐：直接写入
                writeQuote(context, quote);
                _lastQuotes[symbol] = quote;
            } else {
                // 时间戳不对齐：用前后 bar 插值
                interpolateAndWrite(context, symbol, quote, min_t);
            }
        }
        context.SetTime(min_t);
        return NodeProcessResult::Success;
    }

    if (_missingHandle == MissingHandleType::ForwardFill) {
        for (auto& [symbol, quote] : _curQuotes) {
            if (quote._time == min_t) {
                // 时间戳对齐：直接写入
                writeQuote(context, quote);
                _lastQuotes[symbol] = quote;
            } else {
                // 时间戳不对齐：用上一个已知值填充
                forwardFillAndWrite(context, symbol, min_t);
            }
        }
        context.SetTime(min_t);
        return NodeProcessResult::Success;
    }

    if (_missingHandle == MissingHandleType::BackwardFill) {
        for (auto& [symbol, quote] : _curQuotes) {
            if (quote._time == min_t) {
                // 时间戳对齐：直接写入
                writeQuote(context, quote);
                _lastQuotes[symbol] = quote;
            } else {
                // 时间戳不对齐：用下一个已知值填充
                backwardFillAndWrite(context, quote, symbol, min_t);
            }
        }
        context.SetTime(min_t);
        return NodeProcessResult::Success;
    }

    // Skip 模式且不对齐：整批跳过，不写入任何数据
    return NodeProcessResult::Skip;
}

Map<String, ArgType> QuoteInputNode::out_elements() {
    Map<String, ArgType> names;
    for (auto itr = _symbols.begin(); itr != _symbols.end(); ++itr) {
        auto name = get_symbol(*itr);
        auto baseKey = name + ".";
        for (auto& item: _properties[name]) {
            if (item == "volume") {
                names[baseKey + item] = ArgType::Integer_TimeSeries;
            } else {
                names[baseKey + item] = ArgType::Double_TimeSeries;
            }
        }
    }
    return names;
}

const nlohmann::json QuoteInputNode::getParams() {
    return {"code", "freq", "missingHandle"};
}
