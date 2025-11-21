#include "Nodes/QuoteNode.h"
#include "StrategyNode.h"
#include "Util/string_algorithm.h"
#include "Bridge/ETFOptionSymbol.h"
#include "Util/system.h"
#include <ctime>
#include <limits>
#include "server.h"
#include "Bridge/SIM/SIMExchange.h"

namespace {
    std::unordered_map<std::string, std::function<feature_t(const QuoteInfo&)>> propertyHandlers = {
        {"open", [](const QuoteInfo& q) { return q._open; }},
        {"close", [](const QuoteInfo& q) { return q._close; }},
        {"volume", [](const QuoteInfo& q) { return q._volume; }},
        {"turnover", [](const QuoteInfo& q) { return q._turnover; }},
        {"high", [](const QuoteInfo& q) { return q._high; }},
        {"low", [](const QuoteInfo& q) { return q._low; }}
    };
}

QuoteInputNode::QuoteInputNode(Server* server): _server(server) {
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
        if (visited_propers.count(handle))
            continue;

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
        StockSimulation* exchange = (StockSimulation*)_server->GetExchange(ExchangeType::EX_SIM);
        String tickLevel = config["params"]["freq"]["value"];
        if (tickLevel == "1d") {
            exchange->UseLevel(1);
        }
        else {
            exchange->UseLevel(0);
        }
        exchange->SetFilter(filer);
    } else {
        // TODO:
    }

    return true;
}

bool QuoteInputNode::Process(const String& strategy, DataContext& context)
{
    auto cur = context.GetTime();
    // 
    time_t min_t = std::numeric_limits<time_t>::max();
    for (auto itr = _symbols.begin(); itr != _symbols.end(); ++itr) {
        auto symbol = *itr;
        if (is_stock(symbol) || is_etf_option(symbol)) {
            auto stockExchange = _server->GetAvaliableStockExchange();
            auto quote = stockExchange->GetQuote(symbol);
            if (quote._time == 0)
                return false;
            else if (cur != 0 && quote._time <= cur) {
                continue;
            }
            if (quote._time < min_t) { // 注意频率不一致时的填充方式
                min_t = quote._time;
            }

            auto name = get_symbol(symbol);
            auto baseKey = name + ".";
            for (auto& property : _properties[name]) {
                auto it = propertyHandlers.find(property);
                if (it == propertyHandlers.end())
                    continue;

                context.add(baseKey + property, it->second(quote));
            }
        }
        else if (is_option(symbol)) {
            WARN("not implement for input node");
            return false;
        }
    }
    // 
    context.SetTime(min_t);
    return true;
}

Map<String, ArgType> QuoteInputNode::out_elements() {
    Map<String, ArgType> names;
    for (auto itr = _symbols.begin(); itr != _symbols.end(); ++itr) {
        auto name = get_symbol(*itr);
        auto baseKey = name + ".";
        for (auto& item: _properties[name]) {
            if (item == "volume") {
                names[baseKey + item] = ArgType::Integer;
            } else {
                names[baseKey + item] = ArgType::Double;
            }
        }
    }
    return names;
}