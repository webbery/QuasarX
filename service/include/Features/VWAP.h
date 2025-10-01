#pragma once
#include "Feature.h"

class VWAPFeature:  public PrimitiveFeature {
public:
    VWAPFeature(const nlohmann::json& params);

    virtual bool plug(Server* handle, const String& account);

    virtual bool deal(const QuoteInfo& quote, feature_t&);

    virtual const char* desc() {
        return name().c_str();
    }

    virtual FeatureType type() { return FeatureType::FT_VWAP; }
    
    static constexpr String name() {
        return "VWAP";
    }

private:
    int _N = 15;    // 默认统计时长15s

    struct price_info {
        time_t _time;
        double _price;
        uint64_t _volume;
    };
    List<price_info> _prices;
};