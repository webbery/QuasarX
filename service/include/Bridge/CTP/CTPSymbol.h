#pragma once
#include "std_header.h"
#include "Util/system.h"

class CTPSymbol {
public:
    CTPSymbol(const String& symbol);

    operator symbol_t() const;

private:
    String _symbol;
};

String CTPObjectName(int type);
ExchangeName GetExchangeName(const String& object);