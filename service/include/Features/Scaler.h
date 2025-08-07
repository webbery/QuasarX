#pragma once
#include "Feature.h"

class ScalerNode: public PrimitiveFeature {
public:
    ScalerNode(const nlohmann::json& params);
    ~ScalerNode();

    virtual size_t id();
    
    virtual bool plug(Server* handle, const String& account);

    virtual feature_t deal(const QuoteInfo& quote, double extra = 0);

    virtual const char* desc();

    virtual FeatureType type() { return FeatureType::FT_MIN_MAX_SCALER; }

    static constexpr StringView name() {
        return "MinMaxScaler";
    }

private:
};