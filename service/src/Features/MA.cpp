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
}

EMAFeature::~EMAFeature() {

}

size_t EMAFeature::id() {
    String name = desc();
    String sN = std::to_string(_N);
    String sid = name + "_" + sN;
    return std::hash<String>()(sid);
}

bool EMAFeature::plug(Server* handle, const String& account) {
    return true;
}

feature_t EMAFeature::deal(const QuoteInfo& quote, double extra) {
    if (_prev == 0) {
        _prev = _alpha * quote._close;
        return _prev;
    }
    _prev = _alpha * quote._close + _beta * _prev;
    return _prev;
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
}

MACDFeature::~MACDFeature() {

}

size_t MACDFeature::id() {
    return std::hash<StringView>()(name());
}

bool MACDFeature::plug(Server* handle, const String& account) {
    _handle = handle;
    return true;
}

feature_t MACDFeature::deal(const QuoteInfo& quote, double extra) {
    auto itr = _symbolHistory.find(quote._symbol);
    if (itr == _symbolHistory.end()) {
        itr->second = _handle->GetDailyClosePrice(quote._symbol, _signalPeriod, StockAdjustType::None);
    }
    feature_t ret = quote._close;

    Vector<double> slow, fast, signal;
    int outBegin, outEnd;
    TA_EMA(0, itr->second.size(), &(itr->second[0]), _slowPeriod, &outBegin, &outEnd, &(slow[0]));
    TA_EMA(0, itr->second.size(), &(itr->second[0]), _fastPeriod, &outBegin, &outEnd, &(slow[0]));
    TA_EMA(0, itr->second.size(), &(itr->second[0]), _signalPeriod, &outBegin, &outEnd, &(slow[0]));

    // diff = fast - slow
    return ret;
}

const char* MACDFeature::desc() {
    return name().data();
}
