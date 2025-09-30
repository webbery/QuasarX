#pragma once
#include "Feature.h"
#include "Strategy.h"

class BasicFeature: public PrimitiveFeature {
public:
    BasicFeature(const nlohmann::json& params);
    ~BasicFeature();

    virtual bool plug(Server* handle, const String& account);

    virtual bool deal(const QuoteInfo& quote, feature_t&);

    virtual const char* desc();

    virtual FeatureType type() { return FeatureType::FT_EMA; }

    static constexpr StringView name() {
        return BASIC_NAME;
    }

private:
    String _name;   // open/close/high/low/volume/turnover...
};