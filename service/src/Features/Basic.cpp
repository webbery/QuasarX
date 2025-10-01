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
#define PROCESS_QUOTE(name) if (_name == #name) { _prevs = (double)quote._##name; output = _prevs; return true; }
    PROCESS_QUOTE(open);
    PROCESS_QUOTE(close);
    PROCESS_QUOTE(high);
    PROCESS_QUOTE(low);
    PROCESS_QUOTE(turnover);
    PROCESS_QUOTE(volume);
    return false;
}

const char* BasicFeature::desc()
{
    return _name.c_str();
}
