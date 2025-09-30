#include "Features/Basic.h"
#include "Bridge/exchange.h"
#include "Util/string_algorithm.h"

BasicFeature::BasicFeature(const nlohmann::json& params)
{
    _name = to_lower((String)params);
    _id = get_feature_id(desc(), params);
}

BasicFeature::~BasicFeature()
{

}

bool BasicFeature::plug(Server* handle, const String& account)
{
    return true;
}

bool BasicFeature::deal(const QuoteInfo& quote, feature_t& output)
{
    if (!isValid(quote)) {
        output = _prevs;
        return false;
    }
    if (_name == "open") {
        _prevs = quote._open;
        return true;
    }
    else if (_name == "close") {
        _prevs = quote._close;
        return true;
    }
    else if (_name == "high") {
        _prevs = quote._high;
        return true;
    }
    else if (_name == "low") {
        _prevs = quote._low;
        return true;
    }
    else if (_name == "volume") {
        _prevs = (double)quote._volume;
        return true;
    }
    else if (_name == "turnover") {
        _prevs = (double)quote._turnover;
        return true;
    }
    return false;
}

const char* BasicFeature::desc()
{
    return _name.c_str();
}
