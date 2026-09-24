#pragma once
#include "Util/system.h"

class ETFOptionSymbol {
public:
    ETFOptionSymbol(const String& code, const String& name);
    ETFOptionSymbol(symbol_t symbol);

    operator symbol_t() const;

    String name();

    // 从 contract_name 前缀反推交易所: "50ETF购2409月02600" → "SSE",
    // "沪深300ETF沽2409月03500" → "SZSE". 解析失败返回空串.
    static String inferExchangeFromName(const String& contract_name);

private:
    uint64_t GetOptionInfo(const String& name, const String& token, char& month, int& price);

    void SetCode(uint64_t idx, uint64_t id);
    void GetCode(uint64_t& idx, uint64_t& id);

private:
    symbol_t _symbol;
};

symbol_t get_etf_option_symbol(const String& code);
String get_etf_option_code(symbol_t symbol);

// 更新缓存中指定合约的 symbol_t (用于 CSV 导入后从 trade_date 补填 _year)
void update_etf_option_symbol(const String& code, symbol_t sym);