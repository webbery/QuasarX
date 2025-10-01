#include "Features/VWAP.h"
#include "Bridge/exchange.h"

VWAPFeature::VWAPFeature(const nlohmann::json& params) {
    if (params.contains("N")) {
        _N = (int)params["N"]; // default unit is second
    }
    _id = get_feature_id(desc(), params);
}

bool VWAPFeature::plug(Server* handle, const String& account) {
    return true;
}

bool VWAPFeature::deal(const QuoteInfo& quote, feature_t& output) {
    if (!isValid(quote)) {
        output = _prevs;
        return false;
    }
    double price = (quote._high + quote._low + quote._close) / 3;
    _prices.emplace_back(price_info{ quote._time, price, quote._volume });
    auto front = &_prices.front();
    auto& back = _prices.back();
    while (back._time - front->_time > _N) {
        _prices.erase(_prices.begin());
        if (_prices.empty())
            return false;

        front = &_prices.front();
    }
    
    uint64_t total_volumn = 0;
    double n1 = 0;
    for (auto& item : _prices) {
        n1 += item._price * item._volume;
        total_volumn += item._volume;
    }
    _prevs = n1 / total_volumn;
    output = _prevs;
    return true;
}