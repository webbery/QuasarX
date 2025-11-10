#pragma once
#include "Util/system.h"

class ETFOptionSymbol {
public:
    ETFOptionSymbol(const String& symbol);

    operator symbol_t() const;
private:
    String _symbol;
};

String ETFObjectName(int type);

ExchangeName GetETFOptionExchangeName(const String& object);