#include "Features/MA.h"
#include "Bridge/exchange.h"

EMAFeature::EMAFeature(const nlohmann::json& params) {
    if (params.contains("N")) {
        _N = params["N"];
        _alpha = 2.0 /(_N + 1);
    }
}

EMAFeature::~EMAFeature() {

}

bool EMAFeature::plug(Server* handle, const String& account) {
    return true;
}

double EMAFeature::deal(const QuoteInfo& quote) {
    if (_prev == 0) {
        _prev = _alpha * quote._close;
        return _prev;
    }
    _prev = _alpha * quote._close + _prev;
    return _prev;
}

const char* EMAFeature::desc() {
    return "EMA";
}