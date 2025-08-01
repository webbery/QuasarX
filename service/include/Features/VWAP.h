#pragma once
#include "Feature.h"

class VWAPFeature:  public PrimitiveFeature {
public:
    VWAPFeature(const nlohmann::json& params);

    virtual size_t id();

    virtual bool plug(Server* handle, const String& account);

    virtual double deal(const QuoteInfo& quote, double extra = 0);

    virtual const char* desc() {
        return name().data();
    }

    virtual FeatureType type() { return FeatureType::FT_VWAP; }
    
    static constexpr StringView name() {
        return "VWAP";
    }

private:
    int _N = 60;

    struct price_info {
        time_t _time;
        double _price;
        uint64_t _volume;
    };
    List<price_info> _prices;
};