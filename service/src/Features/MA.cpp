#include "Features/MA.h"
#include "Bridge/exchange.h"
#include <functional>
#include "server.h"
#include "ta-lib/ta_libc.h"

EMAFeature::EMAFeature(const nlohmann::json& params) {
    if (params.contains("N")) {
        _N = params["N"];
        _alpha = 2.0 /(_N + 1);
        _beta = (_N - 1)*1.0 / (_N + 1);
    }
    if (params.contains("scaler")) {
        String name = params["scaler"]["type"];
        _scaler = CreateScaler(name, params["scaler"]);
    }
    _id = get_feature_id(desc(), params);
}

EMAFeature::~EMAFeature() {

}

bool EMAFeature::plug(Server* handle, const String& account) {
    return true;
}

bool EMAFeature::deal(const QuoteInfo& quote, feature_t& output) {
    if (!isValid(quote))
        return false;
    if (_prev == 0) {
        _prev = _alpha * quote._close;
        output = _prev;
        return true;
    }
    _prev = _alpha * quote._close + _beta * _prev;
    output = _prev;
    return true;
}

const char* EMAFeature::desc() {
    return "EMA";
}

MACDFeature::MACDFeature(const nlohmann::json& params): _handle(nullptr) {
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
    _id = get_feature_id(desc(), params);
}

MACDFeature::~MACDFeature() {

}

bool MACDFeature::plug(Server* handle, const String& account) {
    _handle = handle;
    return true;
}

bool MACDFeature::deal(const QuoteInfo& quote, feature_t& output) {
    if (!isValid(quote))
        return false;
    auto itr = _symbolHistory.find(quote._symbol);
    if (itr == _symbolHistory.end()) {
        itr->second = _handle->GetDailyClosePrice(quote._symbol, _signalPeriod, StockAdjustType::None);
    }
    output = quote._close;

    Vector<double> slow, fast, signal;
    int outBegin, outEnd;
    TA_EMA(0, itr->second.size(), &(itr->second[0]), _slowPeriod, &outBegin, &outEnd, &(slow[0]));
    TA_EMA(0, itr->second.size(), &(itr->second[0]), _fastPeriod, &outBegin, &outEnd, &(slow[0]));
    TA_EMA(0, itr->second.size(), &(itr->second[0]), _signalPeriod, &outBegin, &outEnd, &(slow[0]));

    // diff = fast - slow
    return true;
}

const char* MACDFeature::desc() {
    return name().data();
}
