#include "Features/ATR.h"
#include "server.h"

constexpr StringView ATRFeature::name() { return "ATR"; }

const char* ATRFeature::desc() { return "ATR"; }

ATRFeature::ATRFeature(const nlohmann::json& params):_sum(0), _close(nullptr) {
    try {
        _T = params["N"];
        _tr.resize(_T);
    } catch(const nlohmann::json::exception& e) {
        WARN("ATRFeature exception: {}", e.what());
    }
}

size_t ATRFeature::id() {
    return std::hash<StringView>()(name());
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

feature_t ATRFeature::deal(const QuoteInfo& quote, double extra) {
    _cnt += 1;
    if (_close == nullptr) {
        _close = new double[_T];
        _close[0] = quote._close;
        return nan("");
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
        return _sum / _T;
    }
    return nan("");
}