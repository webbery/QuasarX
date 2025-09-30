#include "Features/ATR.h"
#include "server.h"


ATRFeature::ATRFeature(const nlohmann::json& params):_sum(0), _close(nullptr) {
    try {
        _T = params["N"];
        _tr.resize(_T);
    } catch(const nlohmann::json::exception& e) {
        WARN("ATRFeature exception: {}", e.what());
    }
    _id = get_feature_id(name(), params);
}

ATRFeature::~ATRFeature() {
    if (_close) delete[] _close;
}

bool ATRFeature::plug(Server* handle, const String& account) {
    // load data
    auto& position = handle->GetPosition(account);
    auto holds = get_holds(position);
    // _data = handle->PrepareData(holds, DataFrequencyType::Day);
    return true;
}

bool ATRFeature::deal(const QuoteInfo& quote, feature_t& output) {
    if (!isValid(quote)) {
        output = _prevs;
        return false;
    }
    _cnt += 1;
    if (_close == nullptr) {
        _close = new double[_T];
        _close[0] = quote._close;
        return false;
    }
    double prev_close = _close[_cur];
    
    double abs1 = quote._high - quote._low;
    double abs2 = fabs(prev_close - quote._high);
    double abs3 = fabs(prev_close - quote._low);
    double tr = std::max(abs3, std::max(abs1, abs2));
    _tr[_cur] = tr;

    _cur = (_cur + 1) % _T;
    _close[_cur] = quote._close;

    _sum += tr;
    if (_cnt >= _T + 1) {
        _sum -= _close[(_cur - 1) % _T];
        _prevs = _sum / _T;
    }
    output = _prevs;
    return true;
}