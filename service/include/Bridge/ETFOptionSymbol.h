#pragma once
#include "Util/system.h"

class ETFOptionSymbol {
public:
    ETFOptionSymbol(const String& code, const String& name);
    ETFOptionSymbol(symbol_t symbol);

    operator symbol_t() const;

    String name();

private:
    uint64_t GetOptionInfo(const String& name, const String& token, char& month, int& price);

    void SetCode(uint64_t idx, uint64_t id);
    void GetCode(uint64_t& idx, uint64_t& id);

private:
    symbol_t _symbol;
};

symbol_t get_etf_option_symbol(const String& code);