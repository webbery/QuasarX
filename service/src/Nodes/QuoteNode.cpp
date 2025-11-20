#include "Nodes/QuoteNode.h"
#include "StrategyNode.h"
#include "Util/string_algorithm.h"
#include "Bridge/ETFOptionSymbol.h"
#include "Util/system.h"
#include <ctime>
#include <limits>

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

bool QuoteInputNode::Init(DataContext& context, const nlohmann::json& config) {
    if (config.empty())
        return true;
    Set<String> pool = config["pool"];
    // stock/option/future
    for (auto& code: pool) {
        List<String> tokens;
        split(code, tokens, ".");
        auto security = Server::GetSecurity(tokens.back());
        if (tokens.back().size() == 6) { // stock
            auto symbol = to_symbol(tokens.back(), tokens.front());
            _symbols.insert(symbol);
        }
        else if (security._exchange == ExchangeName::MT_Shenzhen || security._exchange == ExchangeName::MT_Shanghai) {
            auto symbol = get_etf_option_symbol(tokens.back());
            _symbols.insert(symbol);
        } else {
            WARN("not support symbol for backtest");
            return false;
        }
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

void QuoteInputNode::Connect(QNode* next, const String& from, const String& to) {
    Vector<String> froms;
    split(from, froms, "-");
    QNode::Connect(next, from, to);
    if (froms.size() == 2) {
        // auto id = get_feature_id(froms[1], "");
        for (auto itr = _symbols.begin(); itr != _symbols.end(); ++itr) {
            _properties[get_symbol(*itr)].insert(froms[1]);
        }
    }
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