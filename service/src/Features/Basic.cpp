#include "Features/Basic.h"
#include "Bridge/exchange.h"
#include "Util/string_algorithm.h"

BasicFeature::BasicFeature(const nlohmann::json& params)
{
    _name = to_lower((String)params);
}

BasicFeature::~BasicFeature()
{

}

size_t BasicFeature::id()
{
    String name = desc();
    String sid = name + "_" + _name;
    return std::hash<String>()(sid);
}

bool BasicFeature::plug(Server* handle, const String& account)
{
    return true;
}

feature_t BasicFeature::deal(const QuoteInfo& quote, double extra /*= 0*/)
{
    if (_name == "open") {
        return quote._open;
    }
    if (_name == "close") {
        return quote._close;
    }
    if (_name == "high") {
        return quote._high;
    }
    if (_name == "low") {
        return quote._low;
    }
    if (_name == "volume") {
        return (double)quote._volume;
    }
    if (_name == "turnover") {
        return (double)quote._turnover;
    }
    return 0.;
}

const char* BasicFeature::desc()
{
    return BASIC_NAME;
}
