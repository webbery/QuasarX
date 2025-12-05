#include "Features/MA.h"
#include "Bridge/exchange.h"
#include <functional>
#include "server.h"
#include "ta-lib/ta_libc.h"

namespace {
    double ema(double cur_close, double prev_ema, double coeff) {
        return (cur_close - prev_ema) * coeff + prev_ema;
    }
}
EMAFeature::EMAFeature(const nlohmann::json& params) {
    if (params.contains("N")) {
        _N = params["N"];
        _alpha = 2.0 /(_N + 1);
        _beta = (_N - 1)*1.0 / (_N + 1);
    }
    if (params.contains("scaler")) {
        String name = params["scaler"]["type"];
    }
    _id = get_feature_id(desc(), params);
}

EMAFeature::~EMAFeature() {

}

bool EMAFeature::plug(Server* handle, const String& account) {
    return true;
}

bool EMAFeature::deal(const QuoteInfo& quote, feature_t& output) {
    if (!isValid(quote)) {
        output = _prevs;
        return false;
    }
    if (_prevVal == 0) {
        _prevVal = _alpha * quote._close;
        _prevs = _prevVal;
        output = _prevs;
        return true;
    }
    _prevVal = _alpha * quote._close + _beta * _prevVal;
    _prevs = _prevVal;
    output = _prevs;
    return true;
}

const char* EMAFeature::desc() {
    return "EMA";
}

MACDFeature::MACDFeature(const nlohmann::json& params): _handle(nullptr) {
    _id = get_feature_id(desc(), params);
    if (!check(params, "FastPeriod")) {
        return;
    }
    if (!check(params, "SlowPeriod")) {
        return;
    }
    if (!check(params, "SignalPeriod")) {
        return;
    }
    _fastPeriod = params["FastPeriod"];
    _slowPeriod = params["SlowPeriod"];
    _signalPeriod = params["SignalPeriod"];
}

MACDFeature::~MACDFeature() {

}

bool MACDFeature::plug(Server* handle, const String& account) {
    _handle = handle;
    return true;
}

bool MACDFeature::deal(const QuoteInfo& quote, feature_t& output) {
    if (!isValid(quote)) {
        output = _prevs;
        return false;
    }
    auto itr = _symbolHistory.find(quote._symbol);
    if (itr == _symbolHistory.end() || itr->second.size() < _signalPeriod) {
        _symbolHistory[quote._symbol].push_back(quote._close);
        output = _prevs;
        return false;
        //_symbolHistory[quote._symbol] = _handle->GetDailyClosePrice(quote._symbol, _signalPeriod, StockAdjustType::None);
    }

    _slowEma = ema(quote._close, _slowEma, 2.0 / (_slowPeriod + 1));
    _fastEma = ema(quote._close, _fastEma, 2.0 / (_fastPeriod + 1));
    
    double diff = _fastEma - _slowEma;
    _prevDEA = ema(diff, _prevDEA, _signalPeriod);
    double macd = 2 * (diff - _prevDEA);
    _prevs = Vector<double>{ diff, _prevDEA, macd };
    output = _prevs;
    return true;
}

const char* MACDFeature::desc() {
    return name().data();
}
