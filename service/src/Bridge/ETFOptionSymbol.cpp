#include "std_header.h"
#include "Bridge/ETFOptionSymbol.h"

namespace {
    const Map<char, String> ID2Symbol{
        // szse
        {1, "159901"}, {2, "159915"}, {3, "159919"}, {4, "159922"},
        // sh
        {5, "510050"}
    };

    static Map<String, ExchangeName> exchange_map{
    };
}

String ETFObjectName(int type)
{
    return "";
}

ExchangeName GetETFOptionExchangeName(const String& object)
{
    return ExchangeName::MT_COUNT;
}

ETFOptionSymbol::ETFOptionSymbol(const String& symbol):_symbol(symbol)
{

}

ETFOptionSymbol::operator symbol_t() const
{
    // –Œ»Á510050C2511M02850
    symbol_t symbol;

    return symbol;
}
