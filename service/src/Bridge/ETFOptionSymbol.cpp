#include "std_header.h"
#include "Bridge/ETFOptionSymbol.h"
#include <boost/unordered/concurrent_flat_map.hpp>
#include <cstdint>
#include <cstring>
#include <format>
#include "Util/string_algorithm.h"
#include "Util/datetime.h"

namespace {
    const Map<char, String> ID2Symbol{
        // szse
        {1, "159901"}, {2, "159915"}, {3, "159919"}, {4, "159922"},
        // sh
        {5, "510050"}, {6, "510300"}, {7, "510500"}, {8, "588000"}, {9, "588080"},
    };
    const Map<String, char> name2ID{
        {"深证100ETF", 1}, {"创业板ETF", 2}, {"沪深300ETF", 3}, {"中证500ETF", 4},
        {"50ETF", 5}, {"300ETF", 6}, {"500ETF", 7}, {"科创50", 8}, {"科创板50", 9},
    };
    const Map<char, ExchangeName> ID2Exchange{
        {1, ExchangeName::MT_Shenzhen}, {2, ExchangeName::MT_Shenzhen}, {3, ExchangeName::MT_Shenzhen},{4, ExchangeName::MT_Shenzhen},
        {5, ExchangeName::MT_Shanghai}, {6, ExchangeName::MT_Shanghai}, {7, ExchangeName::MT_Shanghai}, {8, ExchangeName::MT_Shanghai}, {9, ExchangeName::MT_Shanghai},
    };
    // 短编码对应合约编码code
    boost::concurrent_flat_map<uint32_t, String> etf_code_map;
    // 合约编码code对应短编码
    boost::concurrent_flat_map<String, uint32_t> code_etf_map;

    boost::concurrent_flat_map<String, symbol_t> code_symbol_map;
}

String ETFObjectName(int type)
{
    return "";
}

ExchangeName GetETFOptionExchangeName(const String& object)
{
    return ExchangeName::MT_COUNT;
}

ETFOptionSymbol::ETFOptionSymbol(const String& code, const String& name)
{
    if (code_symbol_map.contains(code)) {
        code_symbol_map.visit(code, [this](boost::concurrent_flat_map<String, symbol_t>::value_type& value) {
            _symbol = value.second;
        });
        return;
    }
    auto idx = etf_code_map.size() + 1;
    if (!code_etf_map.contains(code)) {
        code_etf_map.emplace(code, idx);
        etf_code_map.emplace(idx, code);
    }
    contract_type t = contract_type::call;
    char month;
    int price;
    uint64_t id = 0;
    if (name.find("沽") != std::string::npos) {
        t = contract_type::put;
        id = GetOptionInfo(name, "沽", month, price);
    }
    else if (name.find("购") != std::string::npos) {
        t = contract_type::call;
        id = GetOptionInfo(name, "购", month, price);
    }
    else {
        WARN("parse {} fail.", name);
        return;
    }
    
    _symbol._month = month;
    _symbol._price = price;
    _symbol._type = t;
    SetCode(idx, id);
    if (!code_symbol_map.contains(code)) {
        code_symbol_map.emplace(code, _symbol);
    }
}

ETFOptionSymbol::ETFOptionSymbol(symbol_t symbol):_symbol(symbol)
{

}

ETFOptionSymbol::operator symbol_t() const
{
    return _symbol;
}

uint64_t ETFOptionSymbol::GetOptionInfo(const String& name, const String& token, char& month, int& price) {
    Vector<String> tokens;
    split(name, tokens, token.c_str());
    auto itr = name2ID.find(tokens.front());
    if (itr == name2ID.end())
        return 0;

    uint64_t id = itr->second;
    _symbol._exchange = ID2Exchange.at(id);

    Vector<String> info;
    auto back = tokens.back().substr(2);
    split(back, info, "月");
    month = atoi(info.front().c_str());
    price = atoi(info.back().substr(2).c_str());
    return id;
}

void ETFOptionSymbol::SetCode(uint64_t idx, uint64_t id)
{
    _symbol._opt = id;
    _symbol._year = idx;
}

void ETFOptionSymbol::GetCode(uint64_t& idx, uint64_t& id)
{
    id = _symbol._opt;
    idx = _symbol._year;
}

String ETFOptionSymbol::name()
{
    String n;
    uint64_t idx, id;
    GetCode(idx, id);
    auto index = (uint32_t)idx;
    etf_code_map.cvisit_while([&n, idx](const boost::concurrent_flat_map<uint32_t, String>::value_type& val) {
        if (val.first != idx) {
            return true;
        }
        n += val.second;
        return false;
    });
    switch (_symbol._type) {
        case contract_type::call:
            n += "C";
            break;
        case contract_type::put:
            n += "P";
            break;
        default:
            WARN("unknow option type.");
            n += "_";
            break;
    }
    auto mon = _symbol._month;
    n += std::format("{:0^{}}", mon, 2);
    auto price = _symbol._price;
    n += std::format("M{:0^{}}", price, 5);
    // 形如510050C2511M02850
    return n;
}

symbol_t get_etf_option_symbol(const String& code) {
    symbol_t result;
    memset(&result, 0, sizeof(symbol_t));
    if (code_symbol_map.contains(code)) {
        code_symbol_map.visit(code, [&result](boost::concurrent_flat_map<String, symbol_t>::value_type& value) {
            result = value.second;
        });
    }
    return result;
}
