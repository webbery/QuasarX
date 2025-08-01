#include "Features/VWAP.h"
#include "Bridge/exchange.h"

VWAPFeature::VWAPFeature(const nlohmann::json& params) {
    if (params.contains("N")) {
        _N = 60* (int)params["N"]; // default unit is minute
    }
}

size_t VWAPFeature::id() {
    return std::hash<StringView>()(VWAPFeature::name());
}

bool VWAPFeature::plug(Server* handle, const String& account) {
    return true;
}

double VWAPFeature::deal(const QuoteInfo& quote, double extra) {
    double price = (quote._high + quote._low + quote._close) / 3;
    _prices.emplace_back(price_info{ quote._time, price, quote._volume });
    auto front = &_prices.front();
    auto& back = _prices.back();
    while (back._time - front->_time > _N) {
        _prices.erase(_prices.begin());
        if (_prices.empty())
            return nan("");

        front = &_prices.front();
    }
    
    uint64_t total_volumn = 0;
    double n1 = 0;
    for (auto& item : _prices) {
        n1 += item._price * item._volume;
        total_volumn += item._volume;
    }
    return n1 / total_volumn;
}