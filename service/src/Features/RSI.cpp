#include "Features/RSI.h"
#include "Bridge/exchange.h"
#include <cmath>

constexpr StringView RSIFeature::name() { return "ATR"; }

RSIFeature::RSIFeature(const nlohmann::json& params):_prevClose(nan("")) {
    if (params.contains("N")) {
        _lastN = atoi(((String)params["N"]).c_str());
        _diffs.reserve(_lastN);
    }
    _id = get_feature_id(desc(), params);
}

bool RSIFeature::plug(Server* handle, const String& account){
    return true;
}

bool RSIFeature::deal(const QuoteInfo& quote, feature_t& output){
    if (!isValid(quote))
        return false;
    if (!_initialize) {
        return initialize(quote, output);
    }
    
    double val = quote._close - _prevClose;
    if (val > 0) {
        _avgGain = (_avgGain * (_lastN - 1) + val) / _lastN;
    } else {
        _avgLoss = (_avgLoss * (_lastN - 1) - val) / _lastN;
    }
    _diffs[_curIdx] = val;
    _curIdx = (++_curIdx % _lastN);
    output = calculateRSI();
    return true;
}

const char* RSIFeature::desc(){
    return "RSI";
}

feature_t RSIFeature::calculateRSI() const {
    if (_avgLoss == 0.0) {
        return _avgGain > 0.? 100.0: 50.0;
    }
    double rs = _avgGain / _avgLoss;
    return 100.0 - (100.0 / (1.0 + rs));
}

bool RSIFeature::initialize(const QuoteInfo& quote, feature_t& output) {
    if (std::isnan(_prevClose)) {
        _prevClose = quote._close;
        return false;
    }
    double val = quote._close - _prevClose;
    _prevClose = quote._close;
    if (_diffs.size() < _lastN) {
        _diffs.push_back(val);
        return false;
    }
    double initGain = 0;
    double initLoss = 0;
    for (auto val: _diffs) {
        if (val > 0) {
            initGain += val;
        } else {
            initLoss -= val;
        }
    }
    _avgGain = initGain / _lastN;
    _avgLoss = initLoss / _lastN;
    _initialize = true;

    output = calculateRSI();
    return true;
}
